package org.fieldrover;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.hardware.usb.UsbManager;
import android.hardware.usb.UsbDeviceConnection;
import com.hoho.android.usbserial.driver.*;
import java.io.IOException;
import java.util.Arrays;

/** Accessed from the serial worker; enumeration does not open a device. */
public final class Usb {
    private static UsbSerialPort port;
    private static UsbManager manager(Context context) {
        return (UsbManager)context.getSystemService(Context.USB_SERVICE);
    }
    private static String name(UsbSerialPort p) {
        return p.getDriver().getDevice().getDeviceName() + ":" + p.getPortNumber();
    }
    public static String ports(Context context) {
        StringBuilder s = new StringBuilder();
        for (UsbSerialDriver driver : UsbSerialProber.getDefaultProber().findAllDrivers(manager(context)))
            for (UsbSerialPort p : driver.getPorts())
                s.append(name(p)).append('\n');
        return s.toString();
    }
    public static String open(Context context, String path) {
        UsbManager m = manager(context);
        for (UsbSerialDriver driver : UsbSerialProber.getDefaultProber().findAllDrivers(m))
            for (UsbSerialPort p : driver.getPorts()) {
                if (!name(p).equals(path))
                    continue;
                if (!m.hasPermission(driver.getDevice())) {
                    Intent i =
                        new Intent("org.fieldrover.USB_PERMISSION").setPackage(context.getPackageName());
                    m.requestPermission(
                        driver.getDevice(),
                        PendingIntent.getBroadcast(context, 0, i, PendingIntent.FLAG_IMMUTABLE));
                    return "Grant USB permission, then tap Connect again";
                }
                UsbDeviceConnection c = m.openDevice(driver.getDevice());
                if (c == null)
                    return "Could not open USB adapter";
                try {
                    p.open(c);
                    port = p;
                    if (baud(9600))
                        return "";
                    close();
                    return "Unsupported serial settings";
                } catch (Exception ex) {
                    c.close();
                    return ex.toString();
                }
            }
        return "USB adapter not found";
    }
    public static boolean baud(int rate) {
        try {
            port.setParameters(rate, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);
            return true;
        } catch (Exception ex) {
            return false;
        }
    }
    public static boolean write(byte[] bytes) {
        try {
            port.write(bytes, 250);
            return true;
        } catch (Exception ex) {
            return false;
        }
    }
    public static byte[] read() {
        try {
            byte[] buf = new byte[4096];
            return Arrays.copyOf(buf, port.read(buf, 10));
        } catch (Exception ex) {
            return null;
        }
    }
    public static void close() {
        if (port != null) {
            try {
                port.close();
            } catch (IOException ignored) {
            }
            port = null;
        }
    }
}
