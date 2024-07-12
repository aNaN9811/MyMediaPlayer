package com.example.mymediaplayer;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.example.mymediaplayer.databinding.ActivityMainBinding;

import java.io.File;

public class MainActivity extends AppCompatActivity implements SeekBar.OnSeekBarChangeListener {

    private static final int PERMISSION_CODE = 100;
    private boolean hasPermissions = false;
    private boolean hasPrepare = false;
    private MyPlayer player;
    private TextView tv_state;
    private SurfaceView surfaceView;
    private SeekBar seekBar;
    private TextView tv_time;
    private boolean isTouch;
    private int duration;

    @SuppressLint("SetTextI18n")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON, WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        ActivityMainBinding binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        tv_state = findViewById(R.id.tv_state);
        surfaceView = findViewById(R.id.surfaceView);
        tv_time = findViewById(R.id.tv_time);
        seekBar = findViewById(R.id.seekBar);
        seekBar.setOnSeekBarChangeListener(this);

        player = new MyPlayer();
        player.setSurfaceView(surfaceView);
        String path = new File(Environment.getExternalStorageDirectory() + File.separator + "demo.mp4")
                .getAbsolutePath();
        player.setDataSource(path);
        Log.d("MainActivity", "onCreate: " + path);
        player.setOnPreparedListener(() -> {
            duration = player.getDuration();
            runOnUiThread(() -> {
                if (duration != 0) {
                    tv_time.setText("00:00/" + getMinutes(duration) + ":" + getSeconds(duration));
                    tv_time.setVisibility(View.VISIBLE);
                    seekBar.setVisibility(View.VISIBLE);
                }
                tv_state.setTextColor(Color.GREEN);
                tv_state.setText("恭喜init初始化成功");
            });
            player.start();
        });
        player.setOnErrorListener(errorInfo -> runOnUiThread(() -> {
            tv_state.setTextColor(Color.RED);
            tv_state.setText("出错啦，错误:" + errorInfo);
        }));
        player.setOnOnProgressListener(progress -> {
            if (!isTouch) {
                runOnUiThread(new Runnable() {
                    @SuppressLint("SetTextI18n")
                    @Override
                    public void run() {
                        if (duration != 0) {
                            tv_time.setText(getMinutes(progress) + ":" + getSeconds(progress)
                                    + "/" +
                                    getMinutes(duration) + ":" + getSeconds(duration));
                            seekBar.setProgress(progress * 100 / duration);
                        }
                    }
                });
            }
        });
        if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.READ_MEDIA_VIDEO)
                != PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{android.Manifest.permission.CAMERA,
                            Manifest.permission.READ_MEDIA_VIDEO},
                    PERMISSION_CODE);

        } else {
            hasPermissions = true;
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_CODE) {
            if (grantResults.length > 0 &&
                    grantResults[0] == PackageManager.PERMISSION_GRANTED
            ) {
                if (!hasPrepare) {
                    hasPrepare = true;
                    player.prepare();
                }
            } else {
                Toast.makeText(this, "需要相关的权限", Toast.LENGTH_SHORT).show();
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (hasPermissions) {
            if (hasPrepare) {
                player.restart();
            } else {
                hasPrepare = true;
                player.prepare();
            }
        }
    }

    @Override
    protected void onStop() {
        if (hasPermissions) {
            player.stop();
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        if (hasPermissions) {
            player.release();
        }
        super.onDestroy();
    }

    @SuppressLint("SetTextI18n")
    @Override
    public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
        if (fromUser) {
            tv_time.setText(getMinutes(progress * duration / 100)
                    + ":" +
                    getSeconds(progress * duration / 100) + "/" +
                    getMinutes(duration) + ":" + getSeconds(duration));
        }
    }

    @Override
    public void onStartTrackingTouch(SeekBar seekBar) {
        isTouch = true;
    }

    @Override
    public void onStopTrackingTouch(SeekBar seekBar) {
        isTouch = false;
        int seekBarProgress = seekBar.getProgress();
        int playProgress = seekBarProgress * duration / 100;
        player.seek(playProgress);
    }

    private String getMinutes(int duration) {
        int minutes = duration / 60;
        if (minutes <= 9) {
            return "0" + minutes;
        }
        return "" + minutes;
    }

    private String getSeconds(int duration) {
        int seconds = duration % 60;
        if (seconds <= 9) {
            return "0" + seconds;
        }
        return "" + seconds;
    }
}