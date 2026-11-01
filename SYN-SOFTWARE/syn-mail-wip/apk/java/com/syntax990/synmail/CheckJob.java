/* ------------------------------------------------------------------------
 *   New mail, told: every 15 minutes or so, when the phone has a network,
 *   the unread count of each account's inbox is asked for by STATUS (no
 *   folder selected, nothing marked read), and a notification is posted
 *   when it has risen since the last look. Persisted across reboots.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;

import com.syntax990.synmail.core.Account;
import com.syntax990.synmail.core.Mail;

public final class CheckJob extends JobService {
	private static final int JOB = 1;
	private static final String CHANNEL = "mail";
	private volatile Thread worker;

	static void schedule(Context c) {
		JobScheduler js = c.getSystemService(JobScheduler.class);
		if (js == null || js.getPendingJob(JOB) != null) return;
		js.schedule(new JobInfo.Builder(JOB, new ComponentName(c, CheckJob.class))
			.setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
			.setPeriodic(15 * 60 * 1000L)
			.setPersisted(true)
			.build());
	}

	@Override
	public boolean onStartJob(JobParameters p) {
		worker = new Thread(() -> {
			check(this);
			jobFinished(p, false);
		}, "syn-mail-check");
		worker.start();
		return true;
	}

	@Override
	public boolean onStopJob(JobParameters p) {
		Thread w = worker;
		if (w != null) w.interrupt();
		return true;
	}

	static void check(Context c) {
		SharedPreferences seen = c.getSharedPreferences("unseen", MODE_PRIVATE);
		Mail m;
		try {
			m = Env.mail(c);
		} catch (Exception e) {
			return;
		}
		int id = 100;
		for (Account a : Env.store(c).accounts()) {
			id++;
			long n;
			try {
				n = m.inboxUnseen(a);
			} catch (Exception e) {
				continue; /* offline or signed out: said in the app when it is opened, not as a notification every 15 minutes */
			}
			long was = seen.getLong(a.name, 0);
			seen.edit().putLong(a.name, n).apply();
			if (n > was) notify(c, id, a.name, n == 1 ? "1 unread message" : n + " unread messages");
			else if (n == 0) c.getSystemService(NotificationManager.class).cancel(id);
		}
	}

	private static void notify(Context c, int id, String title, String text) {
		NotificationManager nm = c.getSystemService(NotificationManager.class);
		if (nm == null) return;
		nm.createNotificationChannel(new NotificationChannel(CHANNEL, "New mail", NotificationManager.IMPORTANCE_DEFAULT));
		PendingIntent open = PendingIntent.getActivity(c, 0, new Intent(c, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
			PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
		nm.notify(id, new Notification.Builder(c, CHANNEL)
			.setSmallIcon(R.drawable.ic_stat)
			.setContentTitle(title)
			.setContentText(text)
			.setContentIntent(open)
			.setAutoCancel(true)
			.build());
	}
}
