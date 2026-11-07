package com.hashem.wifiscanner;

import android.net.wifi.ScanResult;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * One Wi-Fi network as the user sees it: all access points (routers / bands) that broadcast the
 * same name with the same security are grouped together.
 */
final class WifiNetwork {

    String ssid = "";
    boolean hidden;
    /** Security label, or null when the network is open. */
    String security;
    boolean connected;
    /** Access point with the strongest signal. */
    ScanResult best;
    final List<ScanResult> aps = new ArrayList<>();
    private int bandMask;

    void add(ScanResult r) {
        aps.add(r);
        bandMask |= 1 << WifiUtils.band(r.frequency);
        if (best == null || r.level > best.level) best = r;
    }

    boolean hasBand(int band) {
        return (bandMask & (1 << band)) != 0;
    }

    /** No password needed (plain open or Enhanced Open / OWE). */
    boolean isOpen() {
        return security == null || "OWE".equals(security);
    }

    /** For example "2.4 GHz" or "2.4 + 5 GHz". */
    String bandsLabel() {
        StringBuilder sb = new StringBuilder();
        int[] order = {WifiUtils.BAND_24, WifiUtils.BAND_5, WifiUtils.BAND_6, WifiUtils.BAND_60};
        for (int band : order) {
            if (!hasBand(band)) continue;
            if (sb.length() > 0) sb.append(" + ");
            sb.append(WifiUtils.bandNumber(band));
        }
        if (sb.length() == 0) return best != null ? best.frequency + " MHz" : "";
        return sb.append(" GHz").toString();
    }

    static List<WifiNetwork> group(List<ScanResult> results, String connectedBssid) {
        Map<String, WifiNetwork> map = new LinkedHashMap<>();
        for (ScanResult r : results) {
            if (r == null) continue;
            String name = WifiUtils.cleanSsid(r.SSID);
            boolean hidden = name.isEmpty();
            String sec = WifiUtils.security(r.capabilities);
            String key = hidden
                    ? "\u0001hidden\u0001" + r.BSSID
                    : name + "\u0001" + (sec == null ? "open" : sec);
            WifiNetwork n = map.get(key);
            if (n == null) {
                n = new WifiNetwork();
                n.ssid = name;
                n.hidden = hidden;
                n.security = sec;
                map.put(key, n);
            }
            n.add(r);
            if (connectedBssid != null && connectedBssid.equalsIgnoreCase(r.BSSID)) {
                n.connected = true;
            }
        }

        List<WifiNetwork> list = new ArrayList<>(map.values());
        Comparator<ScanResult> byLevel = new Comparator<ScanResult>() {
            @Override
            public int compare(ScanResult a, ScanResult b) {
                return Integer.compare(b.level, a.level);
            }
        };
        for (WifiNetwork n : list) {
            Collections.sort(n.aps, byLevel);
        }
        Collections.sort(list, new Comparator<WifiNetwork>() {
            @Override
            public int compare(WifiNetwork a, WifiNetwork b) {
                if (a.connected != b.connected) return a.connected ? -1 : 1;
                return Integer.compare(b.best.level, a.best.level);
            }
        });
        return list;
    }
}
