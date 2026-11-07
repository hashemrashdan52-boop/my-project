package com.hashem.wifiscanner;

import android.Manifest;
import android.animation.ValueAnimator;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.Uri;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.Settings;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import java.lang.reflect.Method;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class MainActivity extends Activity {

    private static final int REQ_PERMISSIONS = 42;
    /** Android allows 4 scans per 2 minutes for foreground apps; stay below that. */
    private static final long AUTO_SCAN_INTERVAL_MS = 40000L;
    private static final long SCAN_TIMEOUT_MS = 15000L;
    private static final String PERM_NEARBY_WIFI = "android.permission.NEARBY_WIFI_DEVICES";
    private static final String PREFS = "wifi_scanner";
    private static final String KEY_ASKED = "asked_location";
    private static final String KEY_BLOCKED = "location_blocked";
    private static final String LOCK = "🔒";
    private static final String UNLOCK = "🔓";
    private static final String DOT = " • ";

    private enum Problem {
        NONE, NO_WIFI, NEED_PERMISSION, NEED_PRECISE, PERMISSION_BLOCKED, LOCATION_OFF, WIFI_OFF
    }

    private WifiManager wifi;
    private LocationManager locationManager;
    private SharedPreferences prefs;
    private final Handler handler = new Handler(Looper.getMainLooper());

    private ListView listView;
    private View statusScrim;
    private RadarView radar;
    private TextView countText;
    private TextView summaryText;
    private TextView stat24;
    private TextView stat5;
    private TextView statOpen;
    private TextView statHidden;
    private View statusCard;
    private TextView statusTitle;
    private TextView statusMessage;
    private Button statusButton;
    private Button scanButton;
    private TextView emptyText;

    private final List<WifiNetwork> networks = new ArrayList<>();
    private NetworkAdapter adapter;
    private Problem problem = Problem.NONE;
    private boolean resumed;
    private boolean scanning;
    private boolean receiverRegistered;
    private long lastScanRequest;
    private int shownCount = -1;
    private ValueAnimator countAnimator;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!resumed) return;
            String action = intent.getAction();
            if (WifiManager.SCAN_RESULTS_AVAILABLE_ACTION.equals(action)) {
                onScanResultsAvailable();
            } else {
                refreshState(false);
            }
        }
    };

    private final Runnable autoScan = new Runnable() {
        @Override
        public void run() {
            if (resumed) startScan(false);
        }
    };

    private final Runnable scanTimeout = new Runnable() {
        @Override
        public void run() {
            setScanning(false);
            if (problem == Problem.NONE) showResults();
        }
    };

    // ---------------------------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------------------------

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        locationManager =
                (LocationManager) getApplicationContext().getSystemService(Context.LOCATION_SERVICE);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);

        listView = (ListView) findViewById(R.id.list);
        statusScrim = findViewById(R.id.status_scrim);

        View header = getLayoutInflater().inflate(R.layout.header_main, listView, false);
        View footer = getLayoutInflater().inflate(R.layout.footer_main, listView, false);
        listView.addHeaderView(header, null, false);
        listView.addFooterView(footer, null, false);
        adapter = new NetworkAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                Object item = parent.getItemAtPosition(position);
                if (item instanceof WifiNetwork) showDetails((WifiNetwork) item);
            }
        });

        radar = (RadarView) header.findViewById(R.id.radar);
        countText = (TextView) header.findViewById(R.id.count);
        summaryText = (TextView) header.findViewById(R.id.summary);
        stat24 = setupStat(header, R.id.stat_24, R.string.stat_24);
        stat5 = setupStat(header, R.id.stat_5, R.string.stat_5);
        statOpen = setupStat(header, R.id.stat_open, R.string.stat_open);
        statHidden = setupStat(header, R.id.stat_hidden, R.string.stat_hidden);
        statusCard = header.findViewById(R.id.status_card);
        statusTitle = (TextView) header.findViewById(R.id.status_title);
        statusMessage = (TextView) header.findViewById(R.id.status_message);
        statusButton = (Button) header.findViewById(R.id.status_button);
        scanButton = (Button) header.findViewById(R.id.scan_button);
        emptyText = (TextView) footer.findViewById(R.id.empty_text);

        scanButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (problem == Problem.NONE) {
                    startScan(true);
                } else {
                    onStatusAction();
                }
            }
        });
        statusButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                onStatusAction();
            }
        });

        setupEdgeToEdge();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        registerReceivers();
        radar.setRunning(true);
        refreshState(true);
    }

    @Override
    protected void onPause() {
        super.onPause();
        resumed = false;
        unregisterReceivers();
        handler.removeCallbacks(autoScan);
        handler.removeCallbacks(scanTimeout);
        setScanning(false);
        radar.setRunning(false);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (countAnimator != null) countAnimator.cancel();
        handler.removeCallbacksAndMessages(null);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_PERMISSIONS || grantResults.length == 0) return;
        boolean fine = isGranted(Manifest.permission.ACCESS_FINE_LOCATION);
        boolean blocked = !fine
                && !shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION);
        prefs.edit().putBoolean(KEY_BLOCKED, blocked).apply();
        refreshState(false);
    }

    // ---------------------------------------------------------------------------------------------
    // State: permissions, location, Wi-Fi
    // ---------------------------------------------------------------------------------------------

    private void refreshState(boolean mayAskPermission) {
        problem = detectProblem();
        if (problem == Problem.NEED_PERMISSION && mayAskPermission
                && !prefs.getBoolean(KEY_ASKED, false)) {
            requestScanPermissions();
        }
        renderProblem();

        if (problem == Problem.NONE) {
            showResults();
            long since = SystemClock.elapsedRealtime() - lastScanRequest;
            if (lastScanRequest == 0 || since >= AUTO_SCAN_INTERVAL_MS) {
                startScan(false);
            } else {
                scheduleAutoScan(AUTO_SCAN_INTERVAL_MS - since);
            }
        } else {
            handler.removeCallbacks(autoScan);
            setScanning(false);
            networks.clear();
            adapter.notifyDataSetChanged();
            showUnknownCount();
        }
    }

    private Problem detectProblem() {
        if (wifi == null) return Problem.NO_WIFI;
        boolean fine = isGranted(Manifest.permission.ACCESS_FINE_LOCATION);
        if (!fine) {
            if (prefs.getBoolean(KEY_BLOCKED, false)) return Problem.PERMISSION_BLOCKED;
            boolean coarse = isGranted(Manifest.permission.ACCESS_COARSE_LOCATION);
            if (coarse && Build.VERSION.SDK_INT >= 31) return Problem.NEED_PRECISE;
            return Problem.NEED_PERMISSION;
        }
        if (prefs.getBoolean(KEY_BLOCKED, false)) {
            prefs.edit().putBoolean(KEY_BLOCKED, false).apply();
        }
        if (!isLocationEnabled()) return Problem.LOCATION_OFF;
        if (!isWifiEnabled() && !isScanAlwaysAvailable()) return Problem.WIFI_OFF;
        return Problem.NONE;
    }

    private void renderProblem() {
        switch (problem) {
            case NONE:
                statusCard.setVisibility(View.GONE);
                break;
            case NO_WIFI:
                showStatus(R.string.title_no_wifi, R.string.msg_no_wifi, 0);
                break;
            case NEED_PERMISSION:
                showStatus(R.string.title_permission, R.string.msg_permission, R.string.btn_grant);
                break;
            case NEED_PRECISE:
                showStatus(R.string.title_precise, R.string.msg_precise, R.string.btn_grant);
                break;
            case PERMISSION_BLOCKED:
                showStatus(R.string.title_permission, R.string.msg_permission_blocked,
                        R.string.btn_open_settings);
                break;
            case LOCATION_OFF:
                showStatus(R.string.title_location_off, R.string.msg_location_off,
                        R.string.btn_enable_location);
                break;
            case WIFI_OFF:
                showStatus(R.string.title_wifi_off, R.string.msg_wifi_off,
                        R.string.btn_enable_wifi);
                break;
        }
    }

    private void showStatus(int title, int message, int button) {
        statusCard.setVisibility(View.VISIBLE);
        statusTitle.setText(title);
        statusMessage.setText(message);
        if (button == 0) {
            statusButton.setVisibility(View.GONE);
        } else {
            statusButton.setVisibility(View.VISIBLE);
            statusButton.setText(button);
        }
    }

    private void onStatusAction() {
        switch (problem) {
            case NEED_PERMISSION:
            case NEED_PRECISE:
                requestScanPermissions();
                break;
            case PERMISSION_BLOCKED:
                safeStart(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.fromParts("package", getPackageName(), null)));
                break;
            case LOCATION_OFF:
                safeStart(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS));
                break;
            case WIFI_OFF:
                enableWifi();
                break;
            default:
                break;
        }
    }

    private void requestScanPermissions() {
        List<String> perms = new ArrayList<>();
        perms.add(Manifest.permission.ACCESS_FINE_LOCATION);
        perms.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if (Build.VERSION.SDK_INT >= 33) perms.add(PERM_NEARBY_WIFI);
        prefs.edit().putBoolean(KEY_ASKED, true).apply();
        requestPermissions(perms.toArray(new String[0]), REQ_PERMISSIONS);
    }

    @SuppressWarnings("deprecation")
    private void enableWifi() {
        if (Build.VERSION.SDK_INT >= 29) {
            // Apps can no longer switch Wi-Fi on by themselves; open the quick Wi-Fi panel.
            if (!safeStart(new Intent("android.settings.panel.action.WIFI"))) {
                safeStart(new Intent(Settings.ACTION_WIFI_SETTINGS));
            }
        } else {
            boolean ok = false;
            try {
                ok = wifi.setWifiEnabled(true);
            } catch (SecurityException ignored) {
                // fall through to settings
            }
            if (!ok) safeStart(new Intent(Settings.ACTION_WIFI_SETTINGS));
        }
    }

    private boolean safeStart(Intent intent) {
        try {
            startActivity(intent);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    private boolean isGranted(String permission) {
        return checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean isLocationEnabled() {
        if (locationManager == null) return true;
        if (Build.VERSION.SDK_INT >= 28) {
            try {
                Method m = LocationManager.class.getMethod("isLocationEnabled");
                Object v = m.invoke(locationManager);
                if (v instanceof Boolean) return (Boolean) v;
            } catch (Exception ignored) {
                // fall back to providers below
            }
        }
        try {
            return locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    || locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
        } catch (Exception e) {
            return true;
        }
    }

    private boolean isWifiEnabled() {
        try {
            return wifi.isWifiEnabled();
        } catch (Exception e) {
            return true;
        }
    }

    private boolean isScanAlwaysAvailable() {
        try {
            return wifi.isScanAlwaysAvailable();
        } catch (Exception e) {
            return false;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Scanning
    // ---------------------------------------------------------------------------------------------

    private void registerReceivers() {
        if (receiverRegistered) return;
        IntentFilter filter = new IntentFilter();
        filter.addAction(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
        filter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
        filter.addAction(LocationManager.PROVIDERS_CHANGED_ACTION);
        if (Build.VERSION.SDK_INT >= 33) {
            // Android 13+: state the export flag explicitly (Context.RECEIVER_EXPORTED = 2) so
            // Android 14+ never rejects the registration. All three actions are system broadcasts.
            try {
                Method m = Context.class.getMethod("registerReceiver",
                        BroadcastReceiver.class, IntentFilter.class, int.class);
                m.invoke(this, receiver, filter, 2);
                receiverRegistered = true;
                return;
            } catch (Exception ignored) {
                // fall back to the classic call below
            }
        }
        registerReceiver(receiver, filter);
        receiverRegistered = true;
    }

    private void unregisterReceivers() {
        if (!receiverRegistered) return;
        try {
            unregisterReceiver(receiver);
        } catch (IllegalArgumentException ignored) {
            // already unregistered
        }
        receiverRegistered = false;
    }

    @SuppressLint("MissingPermission")
    @SuppressWarnings("deprecation")
    private void startScan(boolean userInitiated) {
        if (problem != Problem.NONE || scanning) return;
        boolean started;
        try {
            started = wifi.startScan();
        } catch (SecurityException e) {
            started = false;
        }
        lastScanRequest = SystemClock.elapsedRealtime();
        if (started) {
            setScanning(true);
            handler.removeCallbacks(scanTimeout);
            handler.postDelayed(scanTimeout, SCAN_TIMEOUT_MS);
        } else {
            showResults();
            if (userInitiated) {
                Toast.makeText(this, R.string.scan_throttled, Toast.LENGTH_LONG).show();
            }
        }
        scheduleAutoScan(AUTO_SCAN_INTERVAL_MS);
    }

    private void scheduleAutoScan(long delayMs) {
        handler.removeCallbacks(autoScan);
        handler.postDelayed(autoScan, Math.max(1000L, delayMs));
    }

    private void onScanResultsAvailable() {
        if (scanning) {
            handler.removeCallbacks(scanTimeout);
            setScanning(false);
        }
        if (problem == Problem.NONE) showResults();
    }

    private void setScanning(boolean value) {
        scanning = value;
        scanButton.setEnabled(!value);
        scanButton.setText(value ? R.string.scanning : R.string.scan_now);
        radar.setScanning(value);
        if (networks.isEmpty() && emptyText != null) {
            emptyText.setText(value ? R.string.empty_scanning : R.string.empty_networks);
        }
    }

    @SuppressLint("MissingPermission")
    private void showResults() {
        List<ScanResult> results = null;
        try {
            results = wifi.getScanResults();
        } catch (SecurityException ignored) {
            // permission revoked while running
        }
        if (results == null) results = Collections.emptyList();

        networks.clear();
        networks.addAll(WifiNetwork.group(results, connectedBssid()));
        adapter.notifyDataSetChanged();

        int c24 = 0;
        int c5 = 0;
        int open = 0;
        int hidden = 0;
        List<RadarView.Blip> blips = new ArrayList<>();
        for (WifiNetwork n : networks) {
            if (n.hasBand(WifiUtils.BAND_24)) c24++;
            if (n.hasBand(WifiUtils.BAND_5) || n.hasBand(WifiUtils.BAND_6)) c5++;
            if (n.isOpen()) open++;
            if (n.hidden) hidden++;
            blips.add(RadarView.blipFor(n.best.BSSID, n.ssid, n.best.level,
                    getColor(WifiUtils.signalColorRes(n.best.level)), n.connected));
        }
        radar.setBlips(blips);
        animateCount(networks.size());
        stat24.setText(String.valueOf(c24));
        stat5.setText(String.valueOf(c5));
        statOpen.setText(String.valueOf(open));
        statHidden.setText(String.valueOf(hidden));

        String time = new SimpleDateFormat("HH:mm:ss", Locale.US).format(new Date());
        summaryText.setText(getString(R.string.summary_format, String.valueOf(results.size()),
                time));

        emptyText.setVisibility(networks.isEmpty() ? View.VISIBLE : View.GONE);
        emptyText.setText(scanning ? R.string.empty_scanning : R.string.empty_networks);
    }

    @SuppressWarnings("deprecation")
    private String connectedBssid() {
        try {
            WifiInfo info = wifi.getConnectionInfo();
            if (info == null) return null;
            String bssid = info.getBSSID();
            if (bssid == null || "02:00:00:00:00:00".equals(bssid)
                    || "00:00:00:00:00:00".equals(bssid)) {
                return null;
            }
            return bssid;
        } catch (Exception e) {
            return null;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // UI helpers
    // ---------------------------------------------------------------------------------------------

    private TextView setupStat(View header, int id, int label) {
        View card = header.findViewById(id);
        ((TextView) card.findViewById(R.id.stat_label)).setText(label);
        return (TextView) card.findViewById(R.id.stat_value);
    }

    private void showUnknownCount() {
        if (countAnimator != null) countAnimator.cancel();
        shownCount = -1;
        countText.setText(R.string.count_unknown);
        String unknown = getString(R.string.count_unknown);
        stat24.setText(unknown);
        stat5.setText(unknown);
        statOpen.setText(unknown);
        statHidden.setText(unknown);
        summaryText.setText(R.string.summary_idle);
        radar.setBlips(null);
        emptyText.setVisibility(View.GONE);
    }

    private void animateCount(int target) {
        if (countAnimator != null) countAnimator.cancel();
        int from = Math.max(shownCount, 0);
        shownCount = target;
        if (from == target) {
            countText.setText(String.valueOf(target));
            return;
        }
        countAnimator = ValueAnimator.ofInt(from, target);
        countAnimator.setDuration(Math.min(900, 250 + Math.abs(target - from) * 40));
        countAnimator.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override
            public void onAnimationUpdate(ValueAnimator animation) {
                countText.setText(String.valueOf(animation.getAnimatedValue()));
            }
        });
        countAnimator.start();
    }

    @SuppressWarnings("deprecation")
    private void setupEdgeToEdge() {
        Window window = getWindow();
        window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);

        final int padH = dp(16);
        final int padTop = dp(8);
        final int padBottom = dp(24);
        View root = findViewById(R.id.root);
        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                int top = insets.getSystemWindowInsetTop();
                listView.setPadding(padH + insets.getSystemWindowInsetLeft(), top + padTop,
                        padH + insets.getSystemWindowInsetRight(),
                        insets.getSystemWindowInsetBottom() + padBottom);
                ViewGroup.LayoutParams lp = statusScrim.getLayoutParams();
                if (lp.height != top) {
                    lp.height = top;
                    statusScrim.setLayoutParams(lp);
                }
                return insets.consumeSystemWindowInsets();
            }
        });
        root.requestApplyInsets();
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    // ---------------------------------------------------------------------------------------------
    // List
    // ---------------------------------------------------------------------------------------------

    private static final class Holder {
        final View root;
        final SignalBarsView bars;
        final TextView ssid;
        final TextView badge;
        final TextView line2;
        final TextView line3;
        final TextView dbm;
        final TextView quality;

        Holder(View v) {
            root = v;
            bars = (SignalBarsView) v.findViewById(R.id.bars);
            ssid = (TextView) v.findViewById(R.id.ssid);
            badge = (TextView) v.findViewById(R.id.badge);
            line2 = (TextView) v.findViewById(R.id.line2);
            line3 = (TextView) v.findViewById(R.id.line3);
            dbm = (TextView) v.findViewById(R.id.dbm);
            quality = (TextView) v.findViewById(R.id.quality);
        }
    }

    private final class NetworkAdapter extends BaseAdapter {
        @Override
        public int getCount() {
            return networks.size();
        }

        @Override
        public WifiNetwork getItem(int position) {
            return networks.get(position);
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            Holder h;
            if (convertView == null) {
                convertView = getLayoutInflater().inflate(R.layout.item_network, parent, false);
                h = new Holder(convertView);
                convertView.setTag(h);
            } else {
                h = (Holder) convertView.getTag();
            }
            bind(h, networks.get(position));
            return convertView;
        }
    }

    private void bind(Holder h, WifiNetwork n) {
        ScanResult b = n.best;
        int color = getColor(WifiUtils.signalColorRes(b.level));
        h.root.setBackgroundResource(n.connected ? R.drawable.bg_card_connected : R.drawable.bg_card);
        h.ssid.setText(n.hidden ? getString(R.string.hidden_network) : n.ssid);
        h.ssid.setTextColor(getColor(n.hidden ? R.color.muted : R.color.text));
        h.badge.setVisibility(n.connected ? View.VISIBLE : View.GONE);
        h.bars.setSignal(WifiUtils.bars(b.level), color);
        h.dbm.setText(b.level + " dBm");
        h.dbm.setTextColor(color);
        h.quality.setText(WifiUtils.quality(b.level) + "%");
        h.line2.setText(securityAndBandLine(n));
        h.line3.setText(extraLine(n));
    }

    private String securityLabel(WifiNetwork n) {
        if (n.security == null) return UNLOCK + " " + getString(R.string.open_network);
        if ("OWE".equals(n.security)) return UNLOCK + " " + getString(R.string.open_owe);
        return LOCK + " " + WifiUtils.ltr(n.security);
    }

    private String securityAndBandLine(WifiNetwork n) {
        StringBuilder sb = new StringBuilder(securityLabel(n));
        sb.append(DOT).append(WifiUtils.ltr(n.bandsLabel()));
        int ch = WifiUtils.channel(n.best.frequency);
        if (ch > 0) sb.append(DOT).append(getString(R.string.channel)).append(' ').append(ch);
        return sb.toString();
    }

    private String extraLine(WifiNetwork n) {
        ScanResult b = n.best;
        List<String> parts = new ArrayList<>();
        String distance = WifiUtils.formatDistance(WifiUtils.distanceMeters(b.level, b.frequency));
        if (distance != null) parts.add("~" + distance + " " + getString(R.string.meters));
        if (n.aps.size() > 1) {
            parts.add(getString(R.string.access_points, String.valueOf(n.aps.size())));
        }
        String std = WifiUtils.wifiStandardLabel(WifiUtils.wifiStandard(b),
                WifiUtils.band(b.frequency));
        if (std != null) parts.add(WifiUtils.ltr(std));
        StringBuilder sb = new StringBuilder();
        for (String p : parts) {
            if (sb.length() > 0) sb.append(DOT);
            sb.append(p);
        }
        return sb.toString();
    }

    // ---------------------------------------------------------------------------------------------
    // Details dialog
    // ---------------------------------------------------------------------------------------------

    private void showDetails(WifiNetwork n) {
        ScanResult b = n.best;
        View content = getLayoutInflater().inflate(R.layout.dialog_details, null);
        TextView title = (TextView) content.findViewById(R.id.details_title);
        TextView subtitle = (TextView) content.findViewById(R.id.details_subtitle);
        LinearLayout rows = (LinearLayout) content.findViewById(R.id.details_rows);

        title.setText(n.hidden ? getString(R.string.hidden_network) : n.ssid);
        subtitle.setText(n.connected ? getString(R.string.connected)
                : getString(WifiUtils.signalLabelRes(b.level)));

        addRow(rows, R.string.d_security, securityLabel(n));
        addRow(rows, R.string.d_signal, b.level + " dBm  (" + WifiUtils.quality(b.level) + "%)  "
                + getString(WifiUtils.signalLabelRes(b.level)));
        String distance = WifiUtils.formatDistance(WifiUtils.distanceMeters(b.level, b.frequency));
        if (distance != null) {
            addRow(rows, R.string.d_distance, "~" + distance + " " + getString(R.string.meters));
        }
        addRow(rows, R.string.d_band, n.bandsLabel());
        int ch = WifiUtils.channel(b.frequency);
        if (ch > 0) addRow(rows, R.string.d_channel, String.valueOf(ch));
        addRow(rows, R.string.d_frequency, b.frequency + " MHz");
        String width = WifiUtils.channelWidthLabel(b.channelWidth);
        if (width != null) addRow(rows, R.string.d_width, width);
        String std = WifiUtils.wifiStandardLabel(WifiUtils.wifiStandard(b),
                WifiUtils.band(b.frequency));
        if (std != null) addRow(rows, R.string.d_standard, std);
        addRow(rows, R.string.d_bssid, String.valueOf(b.BSSID));
        addRow(rows, R.string.d_aps, String.valueOf(n.aps.size()));
        if (n.aps.size() > 1) {
            StringBuilder sb = new StringBuilder();
            for (ScanResult ap : n.aps) {
                if (sb.length() > 0) sb.append('\n');
                String bandNum = WifiUtils.bandNumber(WifiUtils.band(ap.frequency));
                sb.append(ap.BSSID).append("   ")
                        .append(bandNum != null ? bandNum + " GHz" : ap.frequency + " MHz")
                        .append("   ").append(ap.level).append(" dBm");
            }
            addRow(rows, R.string.d_all_aps, sb.toString());
        }
        if (b.capabilities != null && !b.capabilities.isEmpty()) {
            addRow(rows, R.string.d_capabilities, b.capabilities);
        }

        AlertDialog dialog = new AlertDialog.Builder(this, R.style.DialogTheme)
                .setView(content)
                .setPositiveButton(R.string.ok, null)
                .create();
        dialog.show();
        if (dialog.getWindow() != null) {
            dialog.getWindow().getDecorView().setLayoutDirection(View.LAYOUT_DIRECTION_RTL);
        }
    }

    private void addRow(LinearLayout rows, int label, String value) {
        View row = getLayoutInflater().inflate(R.layout.row_detail, rows, false);
        ((TextView) row.findViewById(R.id.row_label)).setText(label);
        ((TextView) row.findViewById(R.id.row_value)).setText(value);
        rows.addView(row);
    }
}
