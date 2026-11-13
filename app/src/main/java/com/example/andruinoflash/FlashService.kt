package com.example.andruinoflash

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
import android.os.SystemClock
import android.util.Base64
import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import java.io.ByteArrayOutputStream
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
        reporter.info("START")
        startInForeground()

        worker.execute {
            try {
                when (action) {
                    ACTION_DEVICES -> reportUsbDevices(reporter, usbManager)
                    ACTION_BUS -> runBusCommand(
                        reporter,
                        intent?.getStringExtra(EXTRA_BUS_COMMAND)
                            ?: throw IllegalArgumentException("Bus command is missing"),
                        intent?.getStringExtra(EXTRA_USB_DEVICE)
                    )
                    ACTION_RUN -> {
                        val encodedArgs = intent?.getStringExtra(EXTRA_AVRDUDE_ARGS)
                            ?: throw IllegalArgumentException("AVRDUDE arguments are missing")
                        runAvrdude(
                            reporter,
                            decodeArguments(encodedArgs),
                            intent?.getStringExtra(EXTRA_USB_DEVICE),
                            intent?.getIntExtra(EXTRA_UPDI_NODE, 0) ?: 0
                        )
                    }
                    else -> throw IllegalArgumentException(
                        "action must be 'run', 'devices', or 'bus'"
                    )
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
                NotificationChannel(CHANNEL_ID, "Andruino Flash", NotificationManager.IMPORTANCE_LOW)
            )
        }
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        val notification = builder
            .setContentTitle("Andruino Flash operation")
            .setContentText("USB serial operation is running")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setOngoing(true)
            .build()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    private fun reportUsbDevices(reporter: RunReporter, usbManager: UsbManager) {
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        usbManager.deviceList.values.forEach { device ->
            val supported = drivers.any { it.device.deviceName == device.deviceName }
            reporter.info(
                "USB_DEVICE${if (supported) "" else " UNSUPPORTED"} path=${device.deviceName} " +
                    "VID=${device.vendorId.toString(16)} PID=${device.productId.toString(16)} " +
                    "product=${device.productName ?: "unknown"} " +
                    "manufacturer=${device.manufacturerName ?: "unknown"}"
            )
        }
        if (drivers.isEmpty()) {
            throw IOException("No supported USB serial adapter is connected")
        }
        reporter.info("RESULT success")
    }

    private fun runBusCommand(
        reporter: RunReporter,
        command: String,
        requestedDeviceName: String?
    ) {
        if (!BUS_COMMAND_PATTERN.matches(command)) {
            throw IllegalArgumentException(
                "Unsupported bus command. Use PING, STATUS, VERSION, STOP, BLINK, LED, VALVE, MOTOR, MODE, or SELECT syntax."
            )
        }
        val usbManager = getSystemService(USB_SERVICE) as UsbManager
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        if (drivers.isEmpty()) {
            throw IOException("No supported USB serial adapter is connected")
        }
        val driver = if (requestedDeviceName != null) {
            drivers.firstOrNull { it.device.deviceName == requestedDeviceName }
                ?: throw IOException(
                    "USB serial device '$requestedDeviceName' is not connected or is unsupported. " +
                        "Run 'andruino-flash devices' to list supported devices."
                )
        } else {
            if (drivers.size > 1) {
                val paths = drivers.joinToString { it.device.deviceName }
                throw IOException(
                    "Multiple USB serial adapters are connected ($paths). " +
                        "Specify one with --usb-device or run 'andruino-flash devices'."
                )
            }
            drivers.single()
        }
        val device = driver.device
        obtainUsbPermission(usbManager, device)
        val connection = usbManager.openDevice(device)
            ?: throw IOException("Android could not open USB serial device ${device.deviceName}")
        val port = driver.ports.firstOrNull()
            ?: run {
                connection.close()
                throw IOException("USB serial device ${device.deviceName} exposes no serial port")
            }
        val serial = SerialSession(connection, port)
        try {
            port.open(connection)
            port.setParameters(
                CONTROL_BAUD_RATE,
                8,
                UsbSerialPort.STOPBITS_1,
                UsbSerialPort.PARITY_NONE
            )
            // JTAG2UPDI returns to its 19200-baud idle rate after host timeout.
            // Do not toggle DTR here: that would reset the master and discard node mode.
            Thread.sleep(CONTROL_IDLE_SETTLE_MS)

            val payload = "@$command\n".toByteArray(Charsets.US_ASCII)
            port.write(payload, CONTROL_IO_TIMEOUT_MS)
            val response = readControlResponse(port) { candidate ->
                isExpectedBusResponse(command, candidate)
            }
            reporter.info(
                "USB=${device.deviceName} VID=${device.vendorId.toString(16)} " +
                    "PID=${device.productId.toString(16)}"
            )
            reporter.info("BUS_COMMAND $command")
            reporter.info("BUS_RESPONSE $response")
            if (response.startsWith("ERR ")) {
                throw IOException(response)
            }
            reporter.info("RESULT success")
        } finally {
            serial.close()
        }
    }

    private fun setUpdiGate(port: UsbSerialPort, enabled: Boolean, nodeAddress: Int) {
        if (nodeAddress !in 1..7) {
            throw IllegalArgumentException("UPDI node address must be from 1 through 7")
        }
        port.setParameters(
            CONTROL_BAUD_RATE,
            8,
            UsbSerialPort.STOPBITS_1,
            UsbSerialPort.PARITY_NONE
        )
        Thread.sleep(CONTROL_IDLE_SETTLE_MS)
        port.write(
            "@UPDI ${if (enabled) "ENABLE" else "DISABLE"} $nodeAddress\n"
                .toByteArray(Charsets.US_ASCII),
            CONTROL_IO_TIMEOUT_MS
        )
        val response = readControlResponse(port)
        val expectedResponse =
            if (enabled) "NODE $nodeAddress UPDI ENABLED" else "NODE $nodeAddress UPDI DISABLED"
        if (response != expectedResponse) {
            throw IOException("Master UPDI gate command failed: $response")
        }
    }

    private fun resetJtag2updiMaster(port: UsbSerialPort) {
        port.setDTR(false)
        Thread.sleep(250)
        port.setDTR(true)
        Thread.sleep(100)
        port.setDTR(false)
        port.setParameters(
            CONTROL_BAUD_RATE,
            8,
            UsbSerialPort.STOPBITS_1,
            UsbSerialPort.PARITY_NONE
        )
        Thread.sleep(JTAG2UPDI_SETTLE_MS)
    }

    private fun restoreControlBusState(reporter: RunReporter, port: UsbSerialPort): Int? {
        port.setParameters(
            CONTROL_BAUD_RATE,
            8,
            UsbSerialPort.STOPBITS_1,
            UsbSerialPort.PARITY_NONE
        )
        Thread.sleep(CONTROL_IDLE_SETTLE_MS)
        port.write("@RESTORE\n".toByteArray(Charsets.US_ASCII), CONTROL_IO_TIMEOUT_MS)
        val response = readControlResponse(port)
        reporter.info("BUS_RESTORE_RESPONSE $response")
        if (response.startsWith("ERR ")) {
            throw IOException("Master control-bus RESTORE failed: $response")
        }
        if (!response.contains("OK MODE ")) {
            throw IOException("Master control-bus RESTORE returned an unexpected state: $response")
        }
        if (!response.contains("SELECT NONE") && !response.contains("SELECT NODE ")) {
            throw IOException("Master control-bus RESTORE returned an unexpected selection: $response")
        }
        Thread.sleep(JTAG2UPDI_SETTLE_MS)
        return Regex("SELECT NODE ([1-7])$").find(response)?.groupValues?.get(1)?.toInt()
    }

    private fun isExpectedBusResponse(command: String, response: String): Boolean {
        val tokens = command.uppercase().split(Regex("\\s+"))
        return when (tokens.firstOrNull()) {
            "DISCOVER" -> DISCOVER_RESPONSE_PATTERN.matches(response)
            "MODE" -> tokens.size == 2 && response.startsWith("OK MODE ${tokens[1]};")
            "SELECT" -> tokens.size == 2 &&
                response.startsWith("OK SELECT NODE ${tokens[1]}; SYSTEM MODE ")
            "UPDI" -> tokens.size == 3 &&
                response == "NODE ${tokens[2]} UPDI ${if (tokens[1] == "ENABLE") "ENABLED" else "DISABLED"}"
            "PING", "STATUS", "VERSION", "STOP", "READ_ENCODER", "RESET_ENCODER",
            "MOTION_STATUS", "ANGLE", "BLINK", "LED", "VALVE", "MOTOR" -> {
                val node = tokens.getOrNull(1)?.toIntOrNull() ?: return false
                val payload = response.removePrefix("NODE $node ")
                if (payload == response) return false
                when (tokens[0]) {
                    "PING" -> payload == "PONG"
                    "STATUS" -> NODE_STATUS_RESPONSE_PATTERN.matches(response)
                    "VERSION" -> payload.startsWith("VERSION ")
                    "STOP" -> payload.startsWith("STOPPED ")
                    "READ_ENCODER", "RESET_ENCODER" -> payload.startsWith("ENCODER ")
                    "MOTION_STATUS" -> payload.startsWith("MOTION ")
                    "ANGLE" -> payload.startsWith("ANGLE ")
                    "BLINK" -> tokens.getOrNull(2)?.toIntOrNull()?.let { payload == "BLINK $it" } == true
                    "LED", "VALVE" -> tokens.size == 3 && payload == "${tokens[0]} ${tokens[2]}"
                    "MOTOR" -> when (tokens.getOrNull(2)) {
                        "OFF" -> payload == "MOTOR OFF"
                        "FORWARD", "REVERSE" -> tokens.getOrNull(3)?.toIntOrNull()?.let {
                            payload == "MOTOR ${tokens[2]} STARTED MS $it"
                        } == true
                        else -> false
                    }
                    else -> false
                }
            }
            else -> false
        }
    }

    private fun readControlResponse(
        port: UsbSerialPort,
        expectedResponse: ((String) -> Boolean)? = null
    ): String {
        val response = ByteArrayOutputStream()
        val buffer = ByteArray(32)
        val deadline = SystemClock.elapsedRealtime() + CONTROL_RESPONSE_TIMEOUT_MS
        var lastUnexpectedResponse: String? = null
        while (SystemClock.elapsedRealtime() < deadline) {
            val remaining = (deadline - SystemClock.elapsedRealtime()).coerceAtLeast(1)
            val count = port.read(buffer, minOf(CONTROL_IO_TIMEOUT_MS.toLong(), remaining).toInt())
            for (index in 0 until count) {
                val character = buffer[index]
                if (character == '\n'.code.toByte()) {
                    val line = response.toString(Charsets.US_ASCII.name()).trim()
                    if (line.isEmpty()) {
                        throw IOException("Master returned an empty control-bus response")
                    }
                    if (expectedResponse == null || line.startsWith("ERR ") || expectedResponse(line)) {
                        return line
                    }
                    lastUnexpectedResponse = line
                    response.reset()
                    continue
                }
                if (character != '\r'.code.toByte()) {
                    val unsigned = character.toInt() and 0xff
                    if (unsigned < 0x20 || unsigned > 0x7e) continue
                    if (response.size() >= MAX_CONTROL_RESPONSE_LENGTH) {
                        throw IOException("Master control-bus response exceeded the size limit")
                    }
                    response.write(unsigned)
                }
            }
        }
        if (lastUnexpectedResponse != null) {
            throw IOException(
                "Timed out waiting for a response matching the bus command; " +
                    "last unrelated response: $lastUnexpectedResponse"
            )
        }
        throw IOException(
            "Timed out waiting for the master control-bus response. " +
                "Confirm JTAG2UPDI with control-bus support is installed."
        )
    }

    private fun runAvrdude(
        reporter: RunReporter,
        avrdudeArgs: List<String>,
        requestedDeviceName: String?,
        updiNode: Int
    ) {
        val requestId = reporter.requestId
        val usbManager = getSystemService(USB_SERVICE) as UsbManager
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(usbManager)
        if (drivers.isEmpty()) {
            throw IOException("No supported USB serial adapter is connected")
        }
        val driver = if (requestedDeviceName != null) {
            drivers.firstOrNull { it.device.deviceName == requestedDeviceName }
                ?: throw IOException(
                    "USB serial device '$requestedDeviceName' is not connected or is unsupported. " +
                        "Run 'andruino-flash devices' to list supported devices."
                )
        } else {
            if (drivers.size > 1) {
                val paths = drivers.joinToString { it.device.deviceName }
                throw IOException(
                    "Multiple USB serial adapters are connected ($paths). " +
                        "Specify one with --usb-device or run 'andruino-flash devices'."
                )
            }
            drivers.single()
        }
        val device = driver.device
        val baudRateArgument = findOptionValue(avrdudeArgs, "-b")
        val baudRate = baudRateArgument?.toIntOrNull() ?: if (baudRateArgument == null) {
            DEFAULT_BAUD_RATE
        } else {
            throw IllegalArgumentException("Invalid AVRDUDE baud rate: $baudRateArgument")
        }
        if (baudRate <= 0) {
            throw IllegalArgumentException("AVRDUDE baud rate must be positive")
        }
        val usesJtag2updi = findOptionValue(avrdudeArgs, "-c") == "jtag2updi"
        if (usesJtag2updi && updiNode !in 0..7) {
            throw IllegalArgumentException("UPDI node address must be 0 (use selection) or from 1 through 7")
        }

        obtainUsbPermission(usbManager, device)
        val connection = usbManager.openDevice(device)
            ?: throw IOException("Android could not open USB serial device ${device.deviceName}")
        val port = driver.ports.firstOrNull()
            ?: run {
                connection.close()
                throw IOException("USB serial device ${device.deviceName} exposes no serial port")
            }
        val serial = SerialSession(connection, port)
        try {
            port.open(connection)
            if (usesJtag2updi) {
                port.setParameters(
                    CONTROL_BAUD_RATE,
                    8,
                    UsbSerialPort.STOPBITS_1,
                    UsbSerialPort.PARITY_NONE
                )
                reporter.info("USB_RESET skipped to preserve the running JTAG2UPDI master")
            } else {
                port.setParameters(baudRate, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
                port.setDTR(false)
                Thread.sleep(250)
                port.setDTR(true)
                Thread.sleep(100)
                port.setDTR(false)
                Thread.sleep(JTAG2UPDI_SETTLE_MS)
            }
        } catch (error: Exception) {
            serial.close()
            throw error
        }

        var updiGateEnabled = false
        var updiGateNode = updiNode
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

            val safeArgs = enforceAndroidUsbPort(avrdudeArgs)
            val args = mutableListOf(avrdude.absolutePath, "-C", config.absolutePath)
            args.addAll(safeArgs)
            args.addAll(listOf("-P", "COM"))

            reporter.info(
                "USB=${device.deviceName} VID=${device.vendorId.toString(16)} " +
                    "PID=${device.productId.toString(16)} fd=$usbFileDescriptor " +
                    "in=0x${inputEndpoint.toString(16)} out=0x${outputEndpoint.toString(16)}"
            )
            reporter.info("COMMAND ${args.drop(1).joinToString(" ") { shellQuote(it) }}")
            if (usesJtag2updi) {
                reporter.info("BUS_PREP attempting control-bus synchronization")
                try {
                    val selectedNode = restoreControlBusState(reporter, port)
                    val targetNode = when {
                        updiNode == 0 && selectedNode != null -> selectedNode
                        updiNode in 1..7 && selectedNode == updiNode -> updiNode
                        updiNode in 1..7 -> throw IllegalArgumentException(
                            "Selected UPDI node ${selectedNode ?: "none"} does not match requested node $updiNode"
                        )
                        else -> throw IllegalArgumentException(
                            "No UPDI node is selected; issue MODE FLASH and SELECT <node> before flashing"
                        )
                    }
                    updiGateNode = targetNode
                    setUpdiGate(port, enabled = true, nodeAddress = updiGateNode)
                    updiGateEnabled = true
                    reporter.info("BUS_PREP control-bus synchronization succeeded")
                } catch (error: IOException) {
                    reporter.info("BUS_PREP_INFO direct UPDI mode active (control bus inactive or node wired directly to D6): ${error.message}")
                } finally {
                    port.setParameters(baudRate, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
                    Thread.sleep(JTAG2UPDI_SETTLE_MS)
                }
            }
            val nativeResult = try {
                UsbAvrdudeNative.runAvrdude(
                    usbFileDescriptor,
                    inputEndpoint,
                    outputEndpoint,
                    device.vendorId == CH340_VENDOR_ID && device.productId == CH340_PRODUCT_ID,
                    args.toTypedArray()
                )
            } finally {
                if (usesJtag2updi) {
                    reporter.info("USB_RESET restoring the JTAG2UPDI master after AVRDUDE")
                    resetJtag2updiMaster(port)
                }
            }
            val output = nativeResult.getOrNull(1)
                ?: throw IOException("Native AVRDUDE runner returned no output")
            if (updiGateEnabled) {
                try {
                    setUpdiGate(port, enabled = true, nodeAddress = updiGateNode)
                } catch (error: IOException) {
                    reporter.info("UPDI_GATE_WARN could not leave UPDI enabled after programming: ${error.message}")
                }
                updiGateEnabled = true
            }
            output.lineSequence().forEach { line ->
                if (line.isNotEmpty()) {
                    reporter.info("AVRDUDE $line")
                }
            }
            val exitCode = nativeResult.getOrNull(0)?.toIntOrNull()
                ?: throw IOException("Native AVRDUDE runner returned an invalid exit code")
            if (usesJtag2updi) {
                reporter.info("BUS_RESTORE restoring master-persisted mode and selection after AVRDUDE")
                try {
                    restoreControlBusState(reporter, port)
                } catch (error: IOException) {
                    reporter.info("BUS_RESTORE_WARN master control bus remains unavailable: ${error.message}")
                }
            }
            if (exitCode != 0) {
                throw IOException("AVRDUDE exited with status $exitCode")
            }
            reporter.info("RESULT success")
        } finally {
            if (updiGateEnabled) {
                try {
                    setUpdiGate(port, enabled = true, nodeAddress = updiGateNode)
                } catch (error: IOException) {
                    reporter.info("UPDI_GATE_WARN could not restore UPDI after programming: ${error.message}")
                }
            }
            serial.close()
        }
    }

    private fun decodeArguments(encoded: String): List<String> {
        if (encoded.length > MAX_ENCODED_ARGS_LENGTH) {
            throw IllegalArgumentException("AVRDUDE argument payload is too large")
        }
        val bytes = try {
            Base64.decode(encoded, Base64.NO_WRAP)
        } catch (error: IllegalArgumentException) {
            throw IllegalArgumentException("AVRDUDE argument payload is not valid Base64", error)
        }
        val decoded = bytes.toString(Charsets.UTF_8)
        if (!decoded.endsWith('\u0000')) {
            throw IllegalArgumentException("AVRDUDE argument payload is incomplete")
        }
        val arguments = decoded.split('\u0000').dropLast(1)
        if (arguments.isEmpty() || arguments.size > MAX_AVRDUDE_ARGUMENTS ||
            arguments.any { it.isEmpty() || it.length > MAX_ARGUMENT_LENGTH }
        ) {
            throw IllegalArgumentException("AVRDUDE arguments are empty or exceed supported limits")
        }
        return arguments
    }

    private fun findOptionValue(arguments: List<String>, option: String): String? {
        val optionIndex = arguments.indexOfLast { it == option }
        return if (optionIndex >= 0) arguments.getOrNull(optionIndex + 1) else null
    }

    private fun enforceAndroidUsbPort(arguments: List<String>): List<String> {
        val safeArguments = mutableListOf<String>()
        var index = 0
        while (index < arguments.size) {
            val argument = arguments[index]
            when {
                argument == "-P" || argument == "--port" -> {
                    val port = arguments.getOrNull(index + 1)
                        ?: throw IllegalArgumentException("$argument requires a value")
                    if (port != "COM") {
                        throw IllegalArgumentException("The Android USB-FD transport requires port COM")
                    }
                    index += 2
                }
                argument.startsWith("-P") && argument.length > 2 -> {
                    if (argument.removePrefix("-P").removePrefix("=") != "COM") {
                        throw IllegalArgumentException("The Android USB-FD transport requires port COM")
                    }
                    index++
                }
                argument.startsWith("--port=") -> {
                    if (argument.substringAfter('=') != "COM") {
                        throw IllegalArgumentException("The Android USB-FD transport requires port COM")
                    }
                    index++
                }
                else -> {
                    safeArguments.add(argument)
                    index++
                }
            }
        }
        return safeArguments
    }

    private fun shellQuote(value: String): String =
        "'" + value.replace("'", "'\\''") + "'"

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
            throw IOException("USB serial adapter does not expose bulk IN and OUT endpoints")
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
            "AndruinoFlash"
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
        const val EXTRA_AVRDUDE_ARGS = "avrdudeArgs"
        const val EXTRA_BUS_COMMAND = "busCommand"
        const val EXTRA_USB_DEVICE = "usbDevice"
        const val EXTRA_UPDI_NODE = "updiNode"
        const val ACTION_RUN = "run"
        const val ACTION_DEVICES = "devices"
        const val ACTION_BUS = "bus"

        private const val TAG = "AndruinoFlash"
        private const val CHANNEL_ID = "andruino_flash"
        private const val NOTIFICATION_ID = 1101
        private const val CH340_VENDOR_ID = 0x1A86
        private const val CH340_PRODUCT_ID = 0x7523
        private const val DEFAULT_BAUD_RATE = 115200
        private const val CONTROL_BAUD_RATE = 19200
        private const val CONTROL_IDLE_SETTLE_MS = 2500L
        private const val CONTROL_IO_TIMEOUT_MS = 500
        private const val CONTROL_RESPONSE_TIMEOUT_MS = 20000L
        private const val JTAG2UPDI_SETTLE_MS = 4000L
        private const val MAX_CONTROL_RESPONSE_LENGTH = 120
        private val DISCOVER_RESPONSE_PATTERN = Regex("OK NODES (NONE|[1-7](,[1-7])*)")
        private val NODE_STATUS_RESPONSE_PATTERN = Regex(
            "NODE [1-7] STATUS (COMM|FLASH) V=(ON|OFF) " +
                "M=(OFF|FORWARD|REVERSE) L=(ON|OFF) U=(ON|OFF)"
        )
        private val BUS_COMMAND_PATTERN = Regex(
            "(?i)(PING|STATUS|VERSION|STOP|READ_ENCODER|RESET_ENCODER|MOTION_STATUS|ANGLE) [1-7]|BLINK [1-7] [0-9]{2,5}|LED [1-7] (ON|OFF)|MODE (COMM|FLASH)|SELECT [1-7]|DISCOVER|UPDI (ENABLE|DISABLE) [1-7]|VALVE [1-7] (ON|OFF)|MOTOR [1-7] OFF|MOTOR [1-7] (FORWARD|REVERSE) ([1-9][0-9]{0,4})"
        )
        private const val MAX_ENCODED_ARGS_LENGTH = 100_000
        private const val MAX_AVRDUDE_ARGUMENTS = 256
        private const val MAX_ARGUMENT_LENGTH = 16_384
        private val REQUEST_ID_PATTERN = Regex("[A-Za-z0-9._-]{1,80}")
    }
}
