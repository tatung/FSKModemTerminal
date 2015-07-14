package com.akg.sensprout.waterlevel.utils;

import android.content.Context;
import android.widget.Toast;

import java.util.Date;

/**
 * Created by tatung on 7/13/15.
 */
public class DataLogger {
    private Context mContext;
    private static DataLogger mInstance;


    private DataLogger(Context c){
        this.mContext = c;
    }

    public static DataLogger getInstance(Context c){
        if(mInstance == null){
            mInstance = new DataLogger(c.getApplicationContext());
        }
        return mInstance;
    }

    public boolean save(String sensorID, String sDate, int rawData){
        Toast.makeText(mContext, sDate + ": " + sensorID + ": " + rawData, Toast.LENGTH_SHORT).show();
        return true;
    }
}
