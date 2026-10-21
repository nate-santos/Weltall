import android.content.AttributionSource;
import android.content.Context;
import android.content.ContextWrapper;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import java.io.File;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

/** Silent, bounded debug-shell client. No playback, phone-call identity, settings or APK permissions. */
public final class WeltallAudioFocusLock {
    private static String topPackage(Object policy) throws Exception {
        List<?> stack = (List<?>)policy.getClass().getMethod("getFocusStack").invoke(policy);
        if (stack.isEmpty()) return "none";
        Object top = stack.get(stack.size() - 1);
        return (String)top.getClass().getMethod("getPackageName").invoke(top);
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1 || !args[0].matches("/data/local/tmp/weltall-tests/AudioPolicy/[A-Za-z0-9_-]{1,64}")) {
            throw new IllegalArgumentException("Use a new identified AudioPolicy test directory");
        }
        File root = new File(args[0]);
        if (!root.isDirectory() || !root.getCanonicalPath().equals(args[0]) || new File(root, "locked.txt").exists()) {
            throw new IllegalArgumentException("Test directory is absent, redirected, or already used");
        }
        Looper.prepareMainLooper();
        Class<?> threadClass = Class.forName("android.app.ActivityThread");
        Object thread = threadClass.getMethod("systemMain").invoke(null);
        Context system = (Context)threadClass.getMethod("getSystemContext").invoke(thread);
        Context context = new ContextWrapper(system.createPackageContext("com.android.shell", 0)) {
            @Override public String getOpPackageName() { return "com.android.shell"; }
            @Override public Context getApplicationContext() { return this; }
            @Override public AttributionSource getAttributionSource() {
                return new AttributionSource.Builder(android.os.Process.myUid()).setPackageName("com.android.shell").build();
            }
        };
        if (android.os.Process.myUid() != 2000 || context.getApplicationInfo().uid != 2000) {
            throw new IllegalStateException("Run only as the existing debug shell");
        }
        AudioManager audio = AudioManager.class.getConstructor(Context.class).newInstance(context);
        Class<?> policyClass = Class.forName("android.media.audiopolicy.AudioPolicy");
        Class<?> builderClass = Class.forName("android.media.audiopolicy.AudioPolicy$Builder");
        Object policy = builderClass.getMethod("build").invoke(builderClass.getConstructor(Context.class).newInstance(context));
        HandlerThread callbacks = new HandlerThread("WeltallFocusLock");
        callbacks.start();
        AudioFocusRequest focus = null;
        boolean registered = false;
        try {
            int result = (Integer)AudioManager.class.getMethod("registerAudioPolicy", policyClass).invoke(audio, policy);
            if (result != 0) throw new IllegalStateException("Own empty policy registration failed");
            registered = true;
            if (!topPackage(policy).equals("org.weltall.game")) throw new IllegalStateException("Weltall must own focus before the test");
            AudioFocusRequest.Builder builder = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE)
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setOnAudioFocusChangeListener(change -> System.out.println("own_callback=" + change), new Handler(callbacks.getLooper()));
            AudioFocusRequest.Builder.class.getMethod("setLocksFocus", boolean.class).invoke(builder, true);
            focus = builder.build();
            result = (Integer)AudioManager.class.getMethod("requestAudioFocus", AudioFocusRequest.class, policyClass).invoke(audio, focus, policy);
            if (result != AudioManager.AUDIOFOCUS_REQUEST_GRANTED || !topPackage(policy).equals("com.android.shell")) {
                throw new IllegalStateException("Own lock was not granted");
            }
            String record = "uid=2000\nrequest_result=" + result + "\npid=" + android.os.Process.myPid() + "\n";
            Files.write(new File(root, "locked.txt").toPath(), record.getBytes(StandardCharsets.UTF_8));
            System.out.print(record); System.out.flush();
            long deadline = android.os.SystemClock.elapsedRealtime() + 20000;
            File release = new File(root, "release");
            while (!release.exists() && android.os.SystemClock.elapsedRealtime() < deadline) Thread.sleep(100);
            if (!release.exists()) throw new IllegalStateException("Host release not received within 20 seconds");
        } finally {
            if (focus != null) System.out.println("own_focus_abandon=" + audio.abandonAudioFocusRequest(focus));
            if (registered) {
                AudioManager.class.getMethod("unregisterAudioPolicy", policyClass).invoke(audio, policy);
                System.out.println("own_policy_unregistered=true");
            }
            callbacks.quitSafely();
        }
        System.out.flush();
        System.exit(0);
    }
}
