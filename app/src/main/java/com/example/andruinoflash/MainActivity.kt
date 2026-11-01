package com.example.andruinoflash

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import com.hoho.android.usbserial.driver.UsbSerialProber

class MainActivity : Activity() {
    private var usbPermissionReceiver: BroadcastReceiver? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 48, 32, 32)
            gravity = Gravity.CENTER_VERTICAL
        }
        content.addView(TextView(this).apply {
            text = "Connect a supported USB-serial AVR programmer or Arduino board to USB OTG, authorize it, then use the Andruino Flash Termux CLI."
            textSize = 18f
        })
        content.addView(Button(this).apply {
            text = "Authorize USB device"
            setOnClickListener { authorizeUsbDevice() }
        })
        content.addView(Button(this).apply {
            text = "Grant all-files access"
            setOnClickListener {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val settingsIntent = Intent(
                        Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:$packageName")
                    )
                    startActivity(settingsIntent)
                }
            }
        })
        content.addView(TextView(this).apply {
            text = "Grant all-files access once to let the Termux command read firmware paths. Then authorize the USB serial adapter. See the project README for CLI usage."
            textSize = 14f
        })
        setContentView(content)
    }

    override fun onDestroy() {
        usbPermissionReceiver?.let {
            unregisterReceiver(it)
            usbPermissionReceiver = null
        }
        super.onDestroy()
    }

    private fun authorizeUsbDevice() {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val device = UsbSerialProber.getDefaultProber()
            .findAllDrivers(usbManager)
            .firstOrNull()
            ?.device
        if (device == null) {
            Toast.makeText(this, "No supported USB-serial adapter detected", Toast.LENGTH_LONG).show()
            return
        }
        if (usbManager.hasPermission(device)) {
            Toast.makeText(this, "USB access is already authorized", Toast.LENGTH_SHORT).show()
            return
        }

        val action = "$packageName.USB_PERMISSION"
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                usbPermissionReceiver?.let {
                    unregisterReceiver(it)
                    usbPermissionReceiver = null
                }
                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    Toast.makeText(this@MainActivity, "USB access authorized", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this@MainActivity, "USB permission was denied", Toast.LENGTH_LONG).show()
                }
            }
        }
        usbPermissionReceiver = receiver
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(receiver, IntentFilter(action), Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            registerReceiver(receiver, IntentFilter(action))
        }
        val callback = PendingIntent.getBroadcast(
            this,
            device.deviceId,
            Intent(action).setPackage(packageName),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
        )
        usbManager.requestPermission(device, callback)
    }

}
