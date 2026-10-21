package org.weltall.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.view.Gravity;
import android.widget.TextView;

/** A small Java-only foreground bridge while Unreal terminates its old native process. */
public final class WeltallRecoveryActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private int oldPid;
    private long deadline;
    private boolean resumed;
    private boolean launched;
    private TextView message;

    private final Runnable retry = () -> tryReopen();

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        oldPid = getIntent().getIntExtra("oldPid", -1);
        deadline = SystemClock.elapsedRealtime() + 15000;
        message = new TextView(this);
        message.setGravity(Gravity.CENTER);
        message.setTextColor(0xffeeeeee);
        message.setBackgroundColor(0xff101010);
        message.setTextSize(18);
        message.setPadding(32, 24, 32, 24);
        message.setText("Restarting Weltall…\nYour saved games are safe.");
        setContentView(message);
        if (oldPid <= 0 || oldPid == android.os.Process.myPid()) finish();
    }

    @Override public void onResume() {
        super.onResume();
        resumed = true;
        handler.postDelayed(retry, 250);
    }

    @Override public void onPause() {
        resumed = false;
        handler.removeCallbacks(retry);
        super.onPause();
    }

    @Override public void onDestroy() {
        handler.removeCallbacks(retry);
        super.onDestroy();
    }

    private void tryReopen() {
        if (!resumed || launched || isFinishing()) return;
        boolean exited = false;
        try {
            // Signal zero checks existence only; the helper never kills the game or another process.
            Os.kill(oldPid, 0);
        } catch (ErrnoException error) {
            exited = error.errno == OsConstants.ESRCH;
        }
        if (!exited) {
            if (SystemClock.elapsedRealtime() < deadline) handler.postDelayed(retry, 250);
            else message.setText("Weltall could not restart.\nClose the app and open it again.\nYour saved games are safe.");
            return;
        }
        if (!hasWindowFocus()) {
            handler.postDelayed(retry, 250);
            return;
        }
        Intent game = new Intent().setClassName(getPackageName(), "com.epicgames.unreal.GameActivity");
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        game.putExtra("weltallRecoveredActivity", true);
        try {
            launched = true;
            startActivity(game);
            android.util.Log.i("WeltallRecovery", "Reopened after native process " + oldPid + " exited");
            finish();
        } catch (RuntimeException error) {
            android.util.Log.e("WeltallRecovery", "Cannot reopen game", error);
            message.setText("Please close Weltall and open it again.\nYour saved games are safe.");
        }
    }
}
