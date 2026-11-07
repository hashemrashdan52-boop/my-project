package com.hashem.wifiscanner;

import android.net.wifi.ScanResult;
import android.os.Build;

import java.lang.reflect.Method;
import java.util.Locale;

/** Helpers to turn raw scan data into human-readable values. */
final class WifiUtils {

    static final int BAND_UNKNOWN = 0;
    static final int BAND_24 = 1;
    static final int BAND_5 = 2;
    static final int BAND_6 = 3;
    static final int BAND_60 = 4;

    /** Left-to-right mark / right-to-left mark, used to keep Latin fragments intact in Arabic lines. */
    private static final char LRM = '‎';
    private static final char RLM = '‏';

    private static Method sGetWifiStandard;
    private static boolean sWifiStandardLookedUp;

    private WifiUtils() {
    }

    static int band(int freqMhz) {
        if (freqMhz >= 2400 && freqMhz < 2500) return BAND_24;
        if (freqMhz >= 4900 && freqMhz < 5925) return BAND_5;
        if (freqMhz >= 5925 && freqMhz < 7200) return BAND_6;
        if (freqMhz >= 57000 && freqMhz < 72000) return BAND_60;
        return BAND_UNKNOWN;
    }

    static String bandNumber(int band) {
        switch (band) {
            case BAND_24:
                return "2.4";
            case BAND_5:
                return "5";
            case BAND_6:
                return "6";
            case BAND_60:
                return "60";
            default:
                return null;
        }
    }

    static int channel(int freqMhz) {
        if (freqMhz == 2484) return 14;
        if (freqMhz >= 2412 && freqMhz < 2484) return (freqMhz - 2407) / 5;
        if (freqMhz >= 4910 && freqMhz <= 4980) return (freqMhz - 4000) / 5;
        if (freqMhz >= 5000 && freqMhz < 5925) return (freqMhz - 5000) / 5;
        if (freqMhz == 5935) return 2;
        if (freqMhz >= 5950 && freqMhz < 7200) return (freqMhz - 5950) / 5;
        if (freqMhz >= 58320 && freqMhz <= 70200) return (freqMhz - 56160) / 2160;
        return -1;
    }

    /**
     * Security label parsed from {@link ScanResult#capabilities}, or {@code null} for an open
     * network.
     */
    static String security(String capabilities) {
        String c = capabilities == null ? "" : capabilities.toUpperCase(Locale.US);
        if (c.contains("WEP")) return "WEP";
        if (c.contains("OWE")) return "OWE";
        boolean eap = c.contains("EAP");
        boolean sae = c.contains("SAE");
        boolean psk = c.contains("PSK");
        boolean rsn = c.contains("RSN") || c.contains("WPA2");
        boolean wpa1 = c.contains("[WPA-");
        if (eap) {
            if (c.contains("SUITE_B") || c.contains("SUITE-B")) return "WPA3-Enterprise";
            return rsn ? "WPA2-Enterprise" : "WPA-Enterprise";
        }
        if (sae && psk) return "WPA2/WPA3";
        if (sae) return "WPA3";
        if (rsn && wpa1) return "WPA/WPA2";
        if (rsn) return "WPA2";
        if (wpa1 || c.contains("WPA")) return "WPA";
        return null;
    }

    /** Rough signal quality in percent. */
    static int quality(int dbm) {
        int q = 2 * (dbm + 100);
        return Math.max(0, Math.min(100, q));
    }

    /** Number of filled signal bars (1..4). */
    static int bars(int dbm) {
        if (dbm >= -60) return 4;
        if (dbm >= -70) return 3;
        if (dbm >= -80) return 2;
        return 1;
    }

    static int signalColorRes(int dbm) {
        if (dbm >= -60) return R.color.sig_excellent;
        if (dbm >= -70) return R.color.sig_good;
        if (dbm >= -80) return R.color.sig_fair;
        return R.color.sig_weak;
    }

    static int signalLabelRes(int dbm) {
        if (dbm >= -60) return R.string.signal_excellent;
        if (dbm >= -70) return R.string.signal_good;
        if (dbm >= -80) return R.string.signal_fair;
        return R.string.signal_weak;
    }

    /** Free-space path loss estimate. Indoors (walls, people) the real distance is usually shorter. */
    static double distanceMeters(int dbm, int freqMhz) {
        if (freqMhz <= 0) return -1;
        double exp = (27.55 - (20.0 * Math.log10(freqMhz)) + Math.abs(dbm)) / 20.0;
        return Math.pow(10.0, exp);
    }

    static String formatDistance(double meters) {
        if (meters < 0 || Double.isNaN(meters)) return null;
        if (meters < 10) return String.format(Locale.US, "%.1f", Math.max(meters, 0.5));
        if (meters < 1000) return String.valueOf(Math.round(meters));
        return "999+";
    }

    static String channelWidthLabel(int width) {
        switch (width) {
            case 0:
                return "20 MHz";
            case 1:
                return "40 MHz";
            case 2:
                return "80 MHz";
            case 3:
                return "160 MHz";
            case 4:
                return "80+80 MHz";
            case 5:
                return "320 MHz";
            default:
                return null;
        }
    }

    /** {@code ScanResult.getWifiStandard()} (API 30+), looked up reflectively; -1 if unavailable. */
    static int wifiStandard(ScanResult r) {
        if (Build.VERSION.SDK_INT < 30) return -1;
        if (!sWifiStandardLookedUp) {
            sWifiStandardLookedUp = true;
            try {
                sGetWifiStandard = ScanResult.class.getMethod("getWifiStandard");
            } catch (Exception ignored) {
                sGetWifiStandard = null;
            }
        }
        if (sGetWifiStandard == null) return -1;
        try {
            Object v = sGetWifiStandard.invoke(r);
            return v instanceof Integer ? (Integer) v : -1;
        } catch (Exception e) {
            return -1;
        }
    }

    static String wifiStandardLabel(int standard, int band) {
        switch (standard) {
            case 1:
                return "Wi-Fi a/b/g";
            case 4:
                return "Wi-Fi 4";
            case 5:
                return "Wi-Fi 5";
            case 6:
                return band == BAND_6 ? "Wi-Fi 6E" : "Wi-Fi 6";
            case 7:
                return "WiGig";
            case 8:
                return "Wi-Fi 7";
            default:
                return null;
        }
    }

    /** SSID without NUL padding; empty string for hidden networks. */
    static String cleanSsid(String ssid) {
        if (ssid == null) return "";
        String s = ssid.replace("\u0000", "");
        if (s.trim().isEmpty() || "<unknown ssid>".equals(s)) return "";
        return s;
    }

    /** Wraps a Latin fragment so it renders in the right order inside a right-to-left line. */
    static String ltr(String s) {
        return LRM + s + RLM;
    }
}
