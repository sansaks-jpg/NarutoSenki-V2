package re.naruto.game;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.WindowManager;

import org.cocos2dx.lib.Cocos2dxActivity;
import org.cocos2dx.lib.Cocos2dxGLSurfaceView;

public class NarutoSenki extends Cocos2dxActivity {
	private static final String TAG = "NarutoSenki";
	private WifiManager.MulticastLock multicastLock = null;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON, WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

		try {
			WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
			if (wifi != null) {
				multicastLock = wifi.createMulticastLock("nsv2_multicast_lock");
				multicastLock.setReferenceCounted(true);
				multicastLock.acquire();
				Log.d(TAG, "MulticastLock acquired for LAN multiplayer UDP discovery");
			}
		} catch (Exception e) {
			Log.w(TAG, "Failed to acquire MulticastLock: " + e.getMessage());
		}
	}

	@Override
	protected void onDestroy() {
		if (multicastLock != null && multicastLock.isHeld()) {
			try {
				multicastLock.release();
			} catch (Exception ignored) {
			}
		}
		super.onDestroy();
	}

	public Cocos2dxGLSurfaceView onCreateGLSurfaceView() {
		return new LuaGLSurfaceView(this);
	}

	static {
		System.loadLibrary("cocos2dcpp");
	}
}

class LuaGLSurfaceView extends Cocos2dxGLSurfaceView{

	public LuaGLSurfaceView(Context context){
		super(context);
	}

	public boolean onKeyDown(int keyCode, KeyEvent event) {
		// exit program when key back is entered
		if (keyCode == KeyEvent.KEYCODE_BACK) {
			android.os.Process.killProcess(android.os.Process.myPid());
		}
		return super.onKeyDown(keyCode, event);
	}
}
