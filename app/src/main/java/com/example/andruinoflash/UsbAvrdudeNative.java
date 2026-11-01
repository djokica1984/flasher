package com.example.andruinoflash;

public final class UsbAvrdudeNative {
    static {
        System.loadLibrary("usbexec");
    }

    private UsbAvrdudeNative() {
    }

    public static native String[] runAvrdude(
            int usbFileDescriptor,
            int inputEndpoint,
            int outputEndpoint,
            boolean ch340Device,
            String[] arguments);
}
