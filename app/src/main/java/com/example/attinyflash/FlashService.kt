package com.example.attinyflash

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Environment
import android.os.IBinder
import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.Closeable
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class FlashService : Service() {
    private val worker = java.util.concurrent.Executors.newSingleThreadExecutor()

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val requestId = intent?.getStringExtra(EXTRA_REQUEST_ID) ?: UUID.randomUUID().toString()
        val action = intent?.getStringExtra(EXTRA_ACTION)
        if (!REQUEST_ID_PATTERN.matches(requestId)) {
            Log.e(TAG, "Invalid request ID")
            stopSelf(startId)
            return START_NOT_STICKY
        }
        val resultFile = try {
            createResultFile(requestId)
        } catch (error: IOException) {
            Log.e(TAG, "$requestId ERROR cannot create CLI result file: ${error.message}", error)
            stopSelf(startId)
            return START_NOT_STICKY
        }
        if (requestId.startsWith("cli-") && resultFile == null) {
            Log.e(TAG, "$requestId ERROR grant All files access before using the Termux CLI")
            stopSelf(startId)
            return START_NOT_STICKY
        }
        val reporter = RunReporter(requestId, resultFile)
        val usbManager = getSystemService(USB_SERVICE) as UsbManager
        val device = usbManager.deviceList.values.firstOrNull {
            it.vendorId == CH340_VENDOR_ID && it.productId == CH340_PRODUCT_ID
        }
        if (device == null) {
            reporter.error("CH340 adapter 1a86:7523 is not connected")
            stopSelf(startId)
            return START_NOT_STICKY
        }
        if (!usbManager.hasPermission(device)) {
            reporter.error("USB permission is missing; open the app and authorize the adapter first")
            stopSelf(startId)
            return START_NOT_STICKY
        }
        reporter.info("START")
        startInForeground()

        worker.execute {
            try {
                when (action) {
                    ACTION_SIGNATURE -> runAvrdude(reporter, null)
                    ACTION_FLASH -> {
                        val file = intent?.getStringExtra(EXTRA_FIRMWARE)
                            ?: throw IllegalArgumentException("flash requires a firmware path")
                        runAvrdude(reporter, file)
                    }
                    else -> throw IllegalArgumentException("action must be 'signature' or 'flash'")
                }
            } catch (error: Exception) {
                reporter.error(error.message ?: error.javaClass.simpleName, error)
            } finally {
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf(startId)
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        worker.shutdownNow()
        super.onDestroy()
    }

    private fun startInForeground() {
        val manager = getSystemService(NotificationManager::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "ATtiny flashing", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        val notification = builder
            .setContentTitle("ATtiny UPDI operation")
            .setContentText("AVRDUDE is running")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun runAvrdude(reporter: RunReporter, firmwarePath: String?) {
        val requestId = reporter.requestId
        val usbManager = getSystemService(USB_SERVICE) as UsbManager
        val device = usbManager.deviceList.values.firstOrNull {
            it.vendorId == CH340_VENDOR_ID && it.productId == CH340_PRODUCT_ID
        } ?: throw IOException("CH340 adapter 1a86:7523 is not connected")

        val driver = UsbSerialProber.getDefaultProber()
            .findAllDrivers(usbManager)
            .firstOrNull { it.device.deviceId == device.deviceId }
            ?: throw IOException("No supported USB serial driver for 1a86:7523")

        obtainUsbPermission(usbManager, device)
        val connection = usbManager.openDevice(device)
            ?: throw IOException("Android could not open the CH340 device")
        val port = driver.ports.firstOrNull()
            ?: run {
                connection.close()
                throw IOException("The CH340 driver exposes no serial port")
            }
        val serial = SerialSession(connection, port)
        try {
            port.open(connection)
            port.setParameters(BAUD_RATE, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            port.setDTR(false)
            Thread.sleep(250)
            port.setDTR(true)
            Thread.sleep(100)
            port.setDTR(false)
            Thread.sleep(1200)
        } catch (error: Exception) {
            serial.close()
            throw error
        }

        try {
            val avrdude = File(applicationInfo.nativeLibraryDir, "libavrdude.so")
            if (!avrdude.canExecute()) {
                throw IOException("Bundled AVRDUDE is missing or not executable: ${avrdude.path}")
            }
            val usbFileDescriptor = connection.fileDescriptor
            if (usbFileDescriptor < 0) {
                throw IOException("Android did not provide a USB file descriptor")
            }
            val (inputEndpoint, outputEndpoint) = findBulkEndpoints(device)

            val config = File(filesDir, "avrdude.conf")
            if (!config.exists()) {
                assets.open("avrdude.conf").use { source ->
                    config.outputStream().use { destination -> source.copyTo(destination) }
                }
            }

            val args = mutableListOf(
                avrdude.absolutePath,
                "-C", config.absolutePath,
                "-c", "jtag2updi",
                "-P", "COM",
                "-b", BAUD_RATE.toString(),
                "-p", "t1614",
                "-v"
            )
            if (firmwarePath == null) {
                args.addAll(listOf("-U", "signature:r:-:h"))
            } else {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R &&
                    !Environment.isExternalStorageManager()
                ) {
                    throw IOException("Grant all-files access in the app before flashing a shared-storage file")
                }
                val firmware = File(firmwarePath).canonicalFile
                if (!firmware.isFile || !firmware.canRead()) {
                    throw IOException("Firmware is not a readable file: $firmwarePath")
                }
                args.add("-e")
                args.addAll(listOf("-U", "flash:w:${firmware.absolutePath}:i"))
            }

            reporter.info(
                "USB=${device.deviceName} fd=$usbFileDescriptor " +
                    "in=0x${inputEndpoint.toString(16)} out=0x${outputEndpoint.toString(16)}"
            )
            val nativeResult = UsbAvrdudeNative.runAvrdude(
                usbFileDescriptor,
                inputEndpoint,
                outputEndpoint,
                args.toTypedArray()
            )
            val output = nativeResult.getOrNull(1)
                ?: throw IOException("Native AVRDUDE runner returned no output")
            output.lineSequence().forEach { line ->
                if (line.isNotEmpty()) {
                    reporter.info("AVRDUDE $line")
                }
            }
            val exitCode = nativeResult.getOrNull(0)?.toIntOrNull()
                ?: throw IOException("Native AVRDUDE runner returned an invalid exit code")
            if (exitCode != 0) {
                throw IOException("AVRDUDE exited with status $exitCode")
            }
            reporter.info("RESULT success")
        } finally {
            serial.close()
        }
    }

    private fun findBulkEndpoints(device: UsbDevice): Pair<Int, Int> {
        var inputEndpoint = -1
        var outputEndpoint = -1
        for (interfaceIndex in 0 until device.interfaceCount) {
            val usbInterface = device.getInterface(interfaceIndex)
            for (endpointIndex in 0 until usbInterface.endpointCount) {
                val endpoint = usbInterface.getEndpoint(endpointIndex)
                if (endpoint.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
                if (endpoint.direction == UsbConstants.USB_DIR_IN) {
                    inputEndpoint = endpoint.address
                } else {
                    outputEndpoint = endpoint.address
                }
            }
        }
        if (inputEndpoint < 0 || outputEndpoint < 0) {
            throw IOException("CH340 device does not expose bulk IN and OUT endpoints")
        }
        return inputEndpoint to outputEndpoint
    }

    private fun obtainUsbPermission(usbManager: UsbManager, device: UsbDevice) {
        if (usbManager.hasPermission(device)) return

        val permissionGranted = CountDownLatch(1)
        var granted = false
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                permissionGranted.countDown()
            }
        }
        val action = "$packageName.USB_PERMISSION"
        val filter = IntentFilter(action)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            registerReceiver(receiver, filter)
        }
        try {
            val callback = PendingIntent.getBroadcast(
                this,
                device.deviceId,
                Intent(action).setPackage(packageName),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
            )
            usbManager.requestPermission(device, callback)
            if (!permissionGranted.await(90, TimeUnit.SECONDS)) {
                throw IOException("Timed out waiting for USB permission; approve the Android prompt")
            }
            if (!granted) {
                throw IOException("USB permission was denied")
            }
        } finally {
            unregisterReceiver(receiver)
        }
    }

    @Suppress("DEPRECATION")
    private fun createResultFile(requestId: String): File? {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            return null
        }
        val directory = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            "AttinyFlash"
        )
        if (!directory.isDirectory && !directory.mkdirs()) {
            throw IOException("Could not create result directory ${directory.path}")
        }
        val result = File(directory, "$requestId.log")
        FileOutputStream(result, false).use { }
        return result
    }

    private class RunReporter(
        val requestId: String,
        private val resultFile: File?
    ) {
        @Synchronized
        fun info(message: String) {
            Log.i(TAG, "$requestId $message")
            append("$requestId $message")
        }

        @Synchronized
        fun error(message: String, error: Throwable? = null) {
            if (error == null) {
                Log.e(TAG, "$requestId ERROR $message")
            } else {
                Log.e(TAG, "$requestId ERROR $message", error)
            }
            append("$requestId ERROR $message")
        }

        private fun append(line: String) {
            resultFile?.let { file ->
                FileOutputStream(file, true).bufferedWriter().use { writer ->
                    writer.appendLine(line)
                }
            }
        }
    }

    private class SerialSession(
        private val connection: android.hardware.usb.UsbDeviceConnection,
        private val port: UsbSerialPort
    ) : Closeable {
        override fun close() {
            try {
                port.close()
            } finally {
                connection.close()
            }
        }
    }

    companion object {
        const val EXTRA_ACTION = "action"
        const val EXTRA_REQUEST_ID = "requestId"
        const val EXTRA_FIRMWARE = "firmware"
        const val ACTION_SIGNATURE = "signature"
        const val ACTION_FLASH = "flash"

        private const val TAG = "AttinyFlash"
        private const val CHANNEL_ID = "attiny_flash"
        private const val NOTIFICATION_ID = 1614
        private const val CH340_VENDOR_ID = 0x1A86
        private const val CH340_PRODUCT_ID = 0x7523
        private const val BAUD_RATE = 115200
        private val REQUEST_ID_PATTERN = Regex("[A-Za-z0-9._-]{1,80}")
    }
}
