#define _GNU_SOURCE

#include <jni.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static void throw_io_exception(JNIEnv *env, const char *message) {
    jclass exception = (*env)->FindClass(env, "java/io/IOException");
    if (exception != NULL) {
        (*env)->ThrowNew(env, exception, message);
    }
}

static int has_usb_environment_key(const char *entry) {
    return strncmp(entry, "ANDRUINO_USB_FD=", 16) == 0 ||
           strncmp(entry, "ANDRUINO_USB_IN_EP=", 19) == 0 ||
           strncmp(entry, "ANDRUINO_USB_OUT_EP=", 20) == 0 ||
           strncmp(entry, "ANDRUINO_USB_CH340=", 19) == 0;
}

static char **make_child_environment(
        int usb_fd,
        int input_endpoint,
        int output_endpoint,
        int ch340_device) {
    size_t count = 0;
    for (char **entry = environ; entry != NULL && *entry != NULL; entry++) {
        if (!has_usb_environment_key(*entry)) {
            count++;
        }
    }

    char **child_environment = calloc(count + 5, sizeof(char *));
    if (child_environment == NULL) {
        return NULL;
    }

    size_t index = 0;
    for (char **entry = environ; entry != NULL && *entry != NULL; entry++) {
        if (!has_usb_environment_key(*entry)) {
            child_environment[index++] = *entry;
        }
    }

    if (asprintf(&child_environment[index++], "ANDRUINO_USB_FD=%d", usb_fd) < 0 ||
        asprintf(&child_environment[index++], "ANDRUINO_USB_IN_EP=%d", input_endpoint) < 0 ||
        asprintf(&child_environment[index++], "ANDRUINO_USB_OUT_EP=%d", output_endpoint) < 0 ||
        asprintf(&child_environment[index++], "ANDRUINO_USB_CH340=%d", ch340_device) < 0) {
        for (size_t i = count; i < index; i++) {
            free(child_environment[i]);
        }
        free(child_environment);
        return NULL;
    }
    return child_environment;
}

static void free_child_environment(char **child_environment) {
    if (child_environment == NULL) {
        return;
    }
    for (size_t i = 0; child_environment[i] != NULL; i++) {
        if (strncmp(child_environment[i], "ANDRUINO_USB_FD=", 16) == 0 ||
            strncmp(child_environment[i], "ANDRUINO_USB_IN_EP=", 19) == 0 ||
            strncmp(child_environment[i], "ANDRUINO_USB_OUT_EP=", 20) == 0 ||
            strncmp(child_environment[i], "ANDRUINO_USB_CH340=", 19) == 0) {
            free(child_environment[i]);
        }
    }
    free(child_environment);
}

static void free_arguments(char **arguments, jsize length) {
    if (arguments == NULL) {
        return;
    }
    for (jsize i = 0; i < length; i++) {
        free(arguments[i]);
    }
    free(arguments);
}

static char **copy_arguments(JNIEnv *env, jobjectArray java_arguments, jsize *length) {
    *length = (*env)->GetArrayLength(env, java_arguments);
    if (*length < 1) {
        return NULL;
    }

    char **arguments = calloc((size_t) *length + 1, sizeof(char *));
    if (arguments == NULL) {
        return NULL;
    }

    for (jsize i = 0; i < *length; i++) {
        jstring value = (jstring) (*env)->GetObjectArrayElement(env, java_arguments, i);
        if (value == NULL) {
            free_arguments(arguments, *length);
            return NULL;
        }
        const char *utf = (*env)->GetStringUTFChars(env, value, NULL);
        if (utf == NULL) {
            (*env)->DeleteLocalRef(env, value);
            free_arguments(arguments, *length);
            return NULL;
        }
        arguments[i] = strdup(utf);
        (*env)->ReleaseStringUTFChars(env, value, utf);
        (*env)->DeleteLocalRef(env, value);
        if (arguments[i] == NULL) {
            free_arguments(arguments, *length);
            return NULL;
        }
    }
    return arguments;
}

static void child_close_descriptors(int usb_fd, int pipe_write_fd) {
    struct rlimit limit;
    long max_fd = 65536;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < (rlim_t) max_fd) {
        max_fd = (long) limit.rlim_cur;
    }
    for (int fd = STDERR_FILENO + 1; fd < max_fd; fd++) {
        if (fd != usb_fd && fd != pipe_write_fd) {
            close(fd);
        }
    }
}

JNIEXPORT jobjectArray JNICALL
Java_com_example_andruinoflash_UsbAvrdudeNative_runAvrdude(
        JNIEnv *env,
        jclass clazz,
        jint usb_fd,
        jint input_endpoint,
        jint output_endpoint,
        jboolean ch340_device,
        jobjectArray java_arguments) {
    (void) clazz;

    if (usb_fd <= STDERR_FILENO || input_endpoint < 0 || input_endpoint > 255 ||
        output_endpoint < 0 || output_endpoint > 255) {
        throw_io_exception(env, "Invalid Android USB file descriptor or endpoint");
        return NULL;
    }

    jsize argument_count = 0;
    char **arguments = copy_arguments(env, java_arguments, &argument_count);
    if (arguments == NULL) {
        if (!(*env)->ExceptionCheck(env)) {
            throw_io_exception(env, "Could not copy AVRDUDE arguments");
        }
        return NULL;
    }
    const char *executable_path = arguments[0];

    int descriptor_flags = fcntl(usb_fd, F_GETFD);
    if (descriptor_flags < 0) {
        free_arguments(arguments, argument_count);
        throw_io_exception(env, "Android USB file descriptor is not valid");
        return NULL;
    }
    if (fcntl(usb_fd, F_SETFD, descriptor_flags & ~FD_CLOEXEC) < 0) {
        free_arguments(arguments, argument_count);
        throw_io_exception(env, "Could not make Android USB file descriptor inheritable");
        return NULL;
    }

    char **child_environment = make_child_environment(
            usb_fd, input_endpoint, output_endpoint, ch340_device == JNI_TRUE);
    if (child_environment == NULL) {
        fcntl(usb_fd, F_SETFD, descriptor_flags);
        free_arguments(arguments, argument_count);
        throw_io_exception(env, "Could not prepare AVRDUDE USB environment");
        return NULL;
    }

    int output_pipe[2];
    if (pipe(output_pipe) < 0) {
        fcntl(usb_fd, F_SETFD, descriptor_flags);
        free_child_environment(child_environment);
        free_arguments(arguments, argument_count);
        throw_io_exception(env, "Could not create AVRDUDE output pipe");
        return NULL;
    }

    pid_t child = fork();
    if (child < 0) {
        int saved_errno = errno;
        close(output_pipe[0]);
        close(output_pipe[1]);
        fcntl(usb_fd, F_SETFD, descriptor_flags);
        free_child_environment(child_environment);
        free_arguments(arguments, argument_count);
        throw_io_exception(env, strerror(saved_errno));
        return NULL;
    }

    if (child == 0) {
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(output_pipe[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        child_close_descriptors(usb_fd, output_pipe[1]);
        close(output_pipe[1]);
        arguments[0] = (char *) "avrdude";
        execve(executable_path, arguments, child_environment);
        const char message[] = "Failed to execute bundled AVRDUDE\n";
        write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
    }

    close(output_pipe[1]);
    size_t output_length = 0;
    size_t output_capacity = 4096;
    char *output = malloc(output_capacity);
    if (output == NULL) {
        close(output_pipe[0]);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        fcntl(usb_fd, F_SETFD, descriptor_flags);
        free_child_environment(child_environment);
        free_arguments(arguments, argument_count);
        throw_io_exception(env, "Could not allocate AVRDUDE output buffer");
        return NULL;
    }

    char buffer[4096];
    ssize_t bytes_read;
    int read_error = 0;
    while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer))) != 0) {
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            read_error = errno;
            break;
        }
        if (output_length + (size_t) bytes_read + 1 > output_capacity) {
            size_t new_capacity = output_capacity;
            while (new_capacity < output_length + (size_t) bytes_read + 1) {
                if (new_capacity > SIZE_MAX / 2) {
                    read_error = EOVERFLOW;
                    break;
                }
                new_capacity *= 2;
            }
            if (read_error != 0) {
                break;
            }
            char *new_output = realloc(output, new_capacity);
            if (new_output == NULL) {
                read_error = ENOMEM;
                break;
            }
            output = new_output;
            output_capacity = new_capacity;
        }
        memcpy(output + output_length, buffer, (size_t) bytes_read);
        output_length += (size_t) bytes_read;
    }
    close(output_pipe[0]);
    output[output_length] = '\0';

    int wait_status = 0;
    while (waitpid(child, &wait_status, 0) < 0) {
        if (errno != EINTR) {
            read_error = errno;
            break;
        }
    }

    fcntl(usb_fd, F_SETFD, descriptor_flags);
    free_child_environment(child_environment);
    free_arguments(arguments, argument_count);

    if (read_error != 0) {
        free(output);
        throw_io_exception(env, strerror(read_error));
        return NULL;
    }

    int exit_code = WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 128;
    char exit_code_string[16];
    snprintf(exit_code_string, sizeof(exit_code_string), "%d", exit_code);

    jobjectArray result = (*env)->NewObjectArray(
            env, 2, (*env)->FindClass(env, "java/lang/String"), NULL);
    if (result == NULL) {
        free(output);
        return NULL;
    }
    jstring code = (*env)->NewStringUTF(env, exit_code_string);
    jstring log = (*env)->NewStringUTF(env, output);
    free(output);
    if (code == NULL || log == NULL) {
        return NULL;
    }
    (*env)->SetObjectArrayElement(env, result, 0, code);
    (*env)->SetObjectArrayElement(env, result, 1, log);
    return result;
}
