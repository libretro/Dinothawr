package com.dinothawr;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioManager;
import android.os.Bundle;
import android.widget.CheckBox;
import android.widget.CompoundButton;

import com.retroarch.R;

public class Options extends Activity {
	private void bind(int id, final String key, boolean defaultValue) {
		final SharedPreferences prefs = getSharedPreferences(
				Dinothawr.PREFS_NAME, Context.MODE_PRIVATE);

		CheckBox box = (CheckBox) findViewById(id);
		box.setChecked(prefs.getBoolean(key, defaultValue));
		box.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
			@Override
			public void onCheckedChanged(CompoundButton button, boolean checked) {
				prefs.edit().putBoolean(key, checked).apply();
			}
		});
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.options);
		setVolumeControlStream(AudioManager.STREAM_MUSIC);

		bind(R.id.opt_overlay, "overlay_enable", true);
		bind(R.id.opt_purist, "pixel_purist", false);
		bind(R.id.opt_audio, "enable_audio", true);
		bind(R.id.opt_timer, "time_reference", false);
	}
}
