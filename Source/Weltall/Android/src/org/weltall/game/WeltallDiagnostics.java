package org.weltall.game;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Comparator;

/** Explicit local export: bounded metadata/log tail, no save contents or game-data bytes. */
public final class WeltallDiagnostics {
    private static android.util.AtomicFile pending(Context context) {
        return new android.util.AtomicFile(new File(context.getCacheDir(), "weltall-pending-diagnostics.json"));
    }

    /** The system picker can outlive this process; keep only the bounded report until its result. */
    public static void stage(Context context, byte[] bytes) throws java.io.IOException {
        if (bytes.length == 0 || bytes.length > 512 * 1024) throw new java.io.IOException("Invalid report size");
        android.util.AtomicFile file = pending(context);
        java.io.FileOutputStream output = file.startWrite();
        try {
            output.write(bytes);
            file.finishWrite(output);
        } catch (java.io.IOException error) {
            file.failWrite(output);
            throw error;
        }
    }

    public static byte[] restore(Context context) throws java.io.IOException {
        try (FileInputStream input = pending(context).openRead()) {
            java.io.ByteArrayOutputStream output = new java.io.ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > 512 * 1024) throw new java.io.IOException("Invalid pending report size");
                output.write(buffer, 0, count);
            }
            byte[] bytes = output.toByteArray();
            if (bytes.length == 0 || bytes.length > 512 * 1024) throw new java.io.IOException("Invalid pending report size");
            return bytes;
        }
    }

    public static void discard(Context context) { pending(context).delete(); }

    private static String hex(byte[] data) {
        final char[] digits = "0123456789abcdef".toCharArray();
        StringBuilder result = new StringBuilder(data.length * 2);
        for (byte value : data) { result.append(digits[(value & 255) >>> 4]); result.append(digits[value & 15]); }
        return result.toString();
    }

    private static void files(File root, File directory, int depth, JSONArray output, boolean hashes) throws Exception {
        if (depth > 5 || output.length() >= 512) return;
        File[] children = directory.listFiles();
        if (children == null) return;
        Arrays.sort(children, Comparator.comparing(File::getName));
        for (File file : children) {
            if (output.length() >= 512) break;
            // Do not follow links or include anything outside these explicitly selected directories.
            File canonical = file.getCanonicalFile();
            if (!canonical.equals(file.getAbsoluteFile()) || !canonical.toPath().startsWith(root.toPath())) continue;
            if (file.isDirectory()) { files(root, file, depth + 1, output, hashes); continue; }
            if (!file.isFile()) continue;
            JSONObject row = new JSONObject();
            row.put("path", root.toPath().relativize(file.toPath()).toString().replace('\\', '/'));
            row.put("bytes", file.length());
            if (hashes && file.length() <= 1024 * 1024) {
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                try (FileInputStream input = new FileInputStream(file)) {
                    byte[] buffer = new byte[8192];
                    int count, total = 0;
                    while ((count = input.read(buffer)) != -1) {
                        total += count;
                        if (total > 1024 * 1024) throw new java.io.IOException("Player file grew beyond diagnostic hash limit");
                        digest.update(buffer, 0, count);
                    }
                }
                row.put("sha256", hex(digest.digest()));
            }
            output.put(row);
        }
    }

    public static byte[] create(Context context, String label, String logPath) throws Exception {
        JSONObject report = new JSONObject();
        report.put("format", "weltall-android-diagnostics-v1");
        report.put("created_utc", java.time.Instant.now().toString());
        report.put("build", label);
        report.put("phone", Build.MANUFACTURER + " " + Build.MODEL);
        report.put("android", Build.VERSION.RELEASE);
        report.put("sdk", Build.VERSION.SDK_INT);
        report.put("abis", new JSONArray(Arrays.asList(Build.SUPPORTED_ABIS)));
        PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
        report.put("package", info.packageName);
        report.put("version_name", info.versionName);
        report.put("version_code", info.getLongVersionCode());
        if (info.signingInfo != null) report.put("signing_sha256", hex(MessageDigest.getInstance("SHA-256").digest(info.signingInfo.getApkContentsSigners()[0].toByteArray())));
        report.put("apk_bytes", new File(context.getApplicationInfo().sourceDir).length());
        long libraries = 0;
        File[] nativeFiles = new File(context.getApplicationInfo().nativeLibraryDir).listFiles();
        if (nativeFiles != null) for (File file : nativeFiles) if (file.isFile()) libraries += file.length();
        report.put("extracted_native_bytes", libraries);
        report.put("free_storage_bytes", context.getFilesDir().getUsableSpace());
        File root = new File(context.getFilesDir(), "Weltall").getCanonicalFile();
        JSONArray player = new JSONArray();
        files(root, new File(root, "Saves"), 0, player, true);
        files(root, new File(root, "Settings"), 0, player, true);
        report.put("player_files", player);
        JSONArray bundle = new JSONArray();
        files(root, new File(root, "BundledDiscs"), 0, bundle, false);
        report.put("bundle_files", bundle);
        File log = new File(logPath).getCanonicalFile();
        File external = context.getExternalFilesDir(null);
        boolean owned = log.toPath().startsWith(context.getFilesDir().getCanonicalFile().toPath())
            || (external != null && log.toPath().startsWith(external.getCanonicalFile().toPath()));
        if (owned && log.isFile() && log.getName().equals("Weltall.log")) {
            try (RandomAccessFile input = new RandomAccessFile(log, "r")) {
                int count = (int)Math.min(input.length(), 64 * 1024);
                byte[] tail = new byte[count];
                input.seek(input.length() - count);
                input.readFully(tail);
                report.put("log_tail", new String(tail, StandardCharsets.UTF_8));
            }
        }
        report.put("contents", "Build/device details, save/settings fingerprints, bundle sizes and up to 64 KiB of game log. No save or disc contents.");
        byte[] bytes = report.toString(2).getBytes(StandardCharsets.UTF_8);
        if (bytes.length > 512 * 1024) throw new java.io.IOException("Diagnostic report exceeds size limit");
        return bytes;
    }
}
