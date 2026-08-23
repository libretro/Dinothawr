package com.dinothawr;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.media.AudioManager;
import android.os.Bundle;
import android.text.Html;
import android.text.method.LinkMovementMethod;
import android.util.Log;
import android.view.InputDevice;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import com.retroarch.R;
import com.retroarch.browser.retroactivity.RetroActivityFuture;

public class Dinothawr extends Activity {
	private static final String TAG = "Dinothawr";

	/* Subdirectory of the APK assets holding the game data, staged
	 * there by the Gradle build. Extracted verbatim below filesDir. */
	private static final String ASSET_ROOT = "dinothawr";
	private static final String STAMP_FILE = ".assets_version";

	public static final String PREFS_NAME = "dinothawr";

	private volatile boolean extracting = false;

	private SharedPreferences prefs() {
		return getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
	}

	private File gameDir() {
		return new File(getFilesDir(), ASSET_ROOT);
	}

	private File saveDir() {
		File dir = new File(getFilesDir(), "saves");
		dir.mkdirs();
		return dir;
	}

	/* Asset extraction. AssetManager.list() returns an empty array for
	 * regular files and the child names for directories. */

	private void copyAssetFile(AssetManager am, String path, File out)
			throws IOException {
		InputStream in = am.open(path);
		OutputStream os = new BufferedOutputStream(new FileOutputStream(out));
		try {
			byte[] buf = new byte[64 * 1024];
			int len;
			while ((len = in.read(buf)) > 0)
				os.write(buf, 0, len);
		} finally {
			os.close();
			in.close();
		}
	}

	private void copyAssetTree(AssetManager am, String path, File out)
			throws IOException {
		String[] children = am.list(path);
		if (children == null || children.length == 0) {
			copyAssetFile(am, path, out);
			return;
		}

		if (!out.isDirectory() && !out.mkdirs())
			throw new IOException("Failed to create directory: " + out);

		for (String child : children)
			copyAssetTree(am, path + File.separator + child,
					new File(out, child));
	}

	private int extractedVersion() {
		File stamp = new File(getFilesDir(), STAMP_FILE);
		if (!stamp.exists())
			return -1;
		try {
			ConfigFile conf = new ConfigFile(stamp);
			return conf.getInt("version");
		} catch (Exception e) {
			return -1;
		}
	}

	private void writeExtractedVersion(int version) throws IOException {
		ConfigFile conf = new ConfigFile();
		conf.setInt("version", version);
		conf.write(new File(getFilesDir(), STAMP_FILE));
	}

	private int versionCode() {
		try {
			return getPackageManager()
					.getPackageInfo(getPackageName(), 0).versionCode;
		} catch (Exception e) {
			return 0;
		}
	}

	private void maybeExtractAssets() {
		final int version = versionCode();
		if (extractedVersion() == version)
			return;

		final Dialog dialog = new Dialog(this);
		dialog.setCancelable(false);
		dialog.setContentView(R.layout.assets);
		dialog.setTitle("Asset extraction");
		dialog.show();

		extracting = true;
		setStartEnabled(false);

		new Thread(new Runnable() {
			public void run() {
				try {
					copyAssetTree(getAssets(), ASSET_ROOT, gameDir());
					writeExtractedVersion(version);
				} catch (IOException e) {
					Log.e(TAG, "Asset extraction failed!", e);
				}
				runOnUiThread(new Runnable() {
					public void run() {
						extracting = false;
						setStartEnabled(true);
						dialog.dismiss();
					}
				});
			}
		}).start();
	}

	private void setStartEnabled(boolean enable) {
		Button button = (Button) findViewById(R.id.start_button);
		if (button != null)
			button.setEnabled(enable);
	}

	/* On first run, default the overlay off if a gamepad is attached
	 * and on otherwise. The user can change it under Options. */
	private void maybeSetOverlayDefault() {
		SharedPreferences prefs = prefs();
		if (prefs.contains("overlay_enable"))
			return;
		prefs.edit().putBoolean("overlay_enable", !hasGamepad()).apply();
	}

	private boolean hasGamepad() {
		for (int id : InputDevice.getDeviceIds()) {
			InputDevice dev = InputDevice.getDevice(id);
			if (dev == null)
				continue;
			int sources = dev.getSources();
			if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
					|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
				return true;
		}
		return false;
	}

	private boolean isTablet() {
		return getResources().getConfiguration().smallestScreenWidthDp >= 600;
	}

	private int getOptimalSamplingRate() {
		AudioManager manager = (AudioManager) getApplicationContext()
				.getSystemService(Context.AUDIO_SERVICE);
		try {
			return Integer.parseInt(manager
					.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE));
		} catch (Exception e) {
			return 48000;
		}
	}

	/* Saves from the retired launcher lived at the top of dataDir. */
	private void migrateLegacySave() {
		File newSave = new File(saveDir(), "dinothawr.srm");
		if (newSave.exists())
			return;
		File oldSave = new File(getApplicationInfo().dataDir, "dinothawr.srm");
		if (!oldSave.exists())
			return;
		try {
			copyAssetFileFallback(oldSave, newSave);
			Log.i(TAG, "Migrated legacy save file.");
		} catch (IOException e) {
			Log.e(TAG, "Legacy save migration failed.", e);
		}
	}

	private void copyAssetFileFallback(File src, File dst) throws IOException {
		InputStream in = new java.io.FileInputStream(src);
		OutputStream os = new BufferedOutputStream(new FileOutputStream(dst));
		try {
			byte[] buf = new byte[64 * 1024];
			int len;
			while ((len = in.read(buf)) > 0)
				os.write(buf, 0, len);
		} finally {
			os.close();
			in.close();
		}
	}

	private void startGame() {
		if (extracting)
			return;

		SharedPreferences prefs = prefs();
		String gamePath = gameDir().getAbsolutePath() + File.separator;
		String savePath = saveDir().getAbsolutePath();

		migrateLegacySave();

		boolean purist = prefs.getBoolean("pixel_purist", false);
		boolean overlay = prefs.getBoolean("overlay_enable", true);
		String overlayCfg = isTablet() ? "overlay_big.cfg" : "overlay.cfg";

		File coreOptions = new File(getFilesDir(), "dinothawr-core-options.cfg");
		ConfigFile conf = new ConfigFile();
		conf.setString("dino_timer",
				prefs.getBoolean("time_reference", false) ? "enabled"
						: "disabled");
		try {
			conf.write(coreOptions);
		} catch (IOException e) {
			Log.e(TAG, "Failed to write core options.", e);
		}

		conf = new ConfigFile();
		conf.setInt("audio_out_rate", getOptimalSamplingRate());
		conf.setBoolean("audio_enable", prefs.getBoolean("enable_audio", true));
		conf.setBoolean("video_scale_integer", purist);
		conf.setBoolean("video_smooth", !purist);
		conf.setBoolean("video_font_enable", false);
		conf.setBoolean("input_overlay_enable", overlay);
		conf.setString("input_overlay", overlay ? gamePath + overlayCfg : "");
		conf.setDouble("input_overlay_opacity", 0.5);
		conf.setString("menu_driver", "rgui");
		conf.setBoolean("config_save_on_exit", false);
		conf.setString("savefile_directory", savePath);
		conf.setString("savestate_directory", savePath);
		conf.setString("system_directory", gameDir().getAbsolutePath());
		conf.setString("core_options_path", coreOptions.getAbsolutePath());

		File rarchConfig = new File(getFilesDir(), "retroarch.cfg");
		try {
			conf.write(rarchConfig);
		} catch (IOException e) {
			Log.e(TAG, "Failed to write retroarch.cfg.", e);
		}

		Intent intent = new Intent(this, RetroActivityFuture.class);
		intent.putExtra("ROM", gamePath + "dinothawr.game");
		intent.putExtra("LIBRETRO", getApplicationInfo().nativeLibraryDir
				+ File.separator + "libretro.so");
		intent.putExtra("CONFIGFILE", rarchConfig.getAbsolutePath());
		intent.putExtra("DATADIR", getApplicationInfo().dataDir);
		File external = getExternalFilesDir(null);
		if (external != null)
			intent.putExtra("SDCARD", external.getAbsolutePath());
		startActivity(intent);
	}

	private void setupCallbacks() {
		final Context ctx = this;

		Button button = (Button) findViewById(R.id.start_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				startGame();
			}
		});

		button = (Button) findViewById(R.id.controls_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				startActivity(new Intent(ctx, Controls.class));
			}
		});

		button = (Button) findViewById(R.id.instruction_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				startActivity(new Intent(ctx, Instructions.class));
			}
		});

		button = (Button) findViewById(R.id.options_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				startActivity(new Intent(ctx, Options.class));
			}
		});

		button = (Button) findViewById(R.id.credits_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				LayoutInflater inflater = getLayoutInflater();
				View dialog = inflater.inflate(R.layout.credits, null);

				TextView link = (TextView) dialog
						.findViewById(R.id.retroarch_link);
				link.setText(Html.fromHtml(getString(R.string.retroarch)));
				link.setMovementMethod(LinkMovementMethod.getInstance());

				link = (TextView) dialog.findViewById(R.id.libvorbis_link);
				link.setText(Html.fromHtml(getString(R.string.libvorbis)));
				link.setMovementMethod(LinkMovementMethod.getInstance());

				link = (TextView) dialog.findViewById(R.id.pugixml_link);
				link.setText(Html.fromHtml(getString(R.string.pugixml)));
				link.setMovementMethod(LinkMovementMethod.getInstance());

				new AlertDialog.Builder(ctx).setTitle("Credits")
						.setView(dialog).show();
			}
		});

		button = (Button) findViewById(R.id.quit_button);
		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				finish();
			}
		});
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.title);
		setVolumeControlStream(AudioManager.STREAM_MUSIC);

		setupCallbacks();
		maybeSetOverlayDefault();
		maybeExtractAssets();
	}
}
