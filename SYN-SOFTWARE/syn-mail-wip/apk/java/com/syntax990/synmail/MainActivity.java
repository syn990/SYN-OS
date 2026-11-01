/* ------------------------------------------------------------------------
 *   SYN-Mail on the phone: the accounts, their folders, a page of messages,
 *   a message read, a reply or a new message sent, and an account added by
 *   app password or imported from the PC's `syn-mail export` file (sent
 *   over SYN-Link with `syn-link send`). Mail is only ever read with
 *   EXAMINE and BODY.PEEK: nothing is marked read, moved or deleted.
 *   All network work runs off the main thread; a screen that has been
 *   left is not redrawn by a late answer.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.syntax990.synmail.core.Account;
import com.syntax990.synmail.core.Imap;
import com.syntax990.synmail.core.Mail;
import com.syntax990.synmail.core.Msg;
import com.syntax990.synmail.core.Secrets;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
	private static final int REQ_IMPORT = 1, REQ_NOTIFY = 2, PAGE = 25;

	private Palette pal;
	private Store store;
	private final Handler ui = new Handler(Looper.getMainLooper());
	private final ExecutorService bg = Executors.newFixedThreadPool(2);
	/** the screens behind this one, each redrawn by running it */
	private final ArrayDeque<Runnable> stack = new ArrayDeque<>();
	private Runnable here;
	private int gen;
	private TextView status;

	interface Job<T> {
		T run(Mail m) throws Exception;
	}

	interface Done<T> {
		void got(T v);
	}

	@Override
	protected void onCreate(Bundle b) {
		super.onCreate(b);
		pal = Palette.load(this, "");
		store = Env.store(this);
		getWindow().setStatusBarColor(pal.panel);
		getWindow().setNavigationBarColor(pal.bg);
		if (Env.standin(this) && store.accounts().isEmpty()) seedStandin();
		if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
			requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, REQ_NOTIFY);
		}
		CheckJob.schedule(this);
		go(this::home);
	}

	@Override
	protected void onDestroy() {
		bg.shutdownNow();
		super.onDestroy();
	}

	/* ---------- screens and the work behind them ---------- */

	private void go(Runnable screen) {
		if (here != null) stack.push(here);
		here = screen;
		screen.run();
	}

	@Override
	@SuppressWarnings("deprecation")
	public void onBackPressed() {
		if (stack.isEmpty()) {
			super.onBackPressed();
			return;
		}
		here = stack.pop();
		here.run();
	}

	private void again() {
		here.run();
	}

	/** a fresh screen: header, status line, the body to fill, the buttons at the foot */
	private LinearLayout[] frame(String where) {
		gen++;
		LinearLayout root = new LinearLayout(this);
		root.setOrientation(LinearLayout.VERTICAL);
		root.setBackgroundColor(pal.bg);
		root.setFitsSystemWindows(true);
		root.addView(Views.header(this, pal, where));
		root.addView(Views.rule(this, pal));
		status = Views.text(this, pal, "", 13, false, Palette.mix(pal.text, pal.bg, 25));
		int pad = Views.dp(this, 10);
		status.setPadding(pad, pad / 2, pad, pad / 2);
		status.setVisibility(View.GONE);
		root.addView(status);
		ScrollView sv = new ScrollView(this);
		LinearLayout body = new LinearLayout(this);
		body.setOrientation(LinearLayout.VERTICAL);
		sv.addView(body);
		root.addView(sv, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
		LinearLayout foot = new LinearLayout(this);
		foot.setOrientation(LinearLayout.HORIZONTAL);
		foot.setPadding(pad / 2, pad / 2, pad / 2, pad / 2);
		root.addView(foot);
		setContentView(root);
		return new LinearLayout[] {body, foot};
	}

	private void say(String s, boolean bad) {
		status.setText(s);
		status.setTextColor(bad ? pal.urgent : Palette.mix(pal.text, pal.bg, 25));
		status.setVisibility(s.isEmpty() ? View.GONE : View.VISIBLE);
	}

	private void addButton(LinearLayout foot, String label, boolean primary, View.OnClickListener on) {
		TextView b = Views.button(this, pal, label, primary, on);
		LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
		int m = Views.dp(this, 4);
		lp.setMargins(m, 0, m, 0);
		foot.addView(b, lp);
	}

	/** runs the job off the main thread; its answer reaches done only if this screen is still showing */
	private <T> void work(String busy, Job<T> job, Done<T> done) {
		final int g = gen;
		if (!busy.isEmpty()) say(busy, false);
		bg.execute(() -> {
			try {
				Mail m = Env.mail(this);
				T v = job.run(m);
				ui.post(() -> {
					if (g != gen || isFinishing()) return;
					if (!busy.isEmpty()) say("", false);
					done.got(v);
				});
			} catch (Exception e) {
				String why = e.getMessage() == null ? e.toString() : e.getMessage();
				ui.post(() -> {
					if (g == gen && !isFinishing()) say(why, true);
				});
			}
		});
	}

	/* ---------- home: the accounts ---------- */

	private void home() {
		LinearLayout[] f = frame(Env.standin(this) ? "stand-in" : "accounts");
		List<Account> list = store.accounts();
		if (list.isEmpty()) {
			TextView t = Views.text(this, pal, "No account on this phone yet.\n\nImport from PC: on the PC run\n  syn-mail export NAME FILE\n"
				+ "  syn-link send FILE\nthen pick the file here. It is deleted once read.\n\nOr add one with an app password.", 14, false, pal.text);
			int pad = Views.dp(this, 14);
			t.setPadding(pad, pad, pad, pad);
			f[0].addView(t);
		}
		for (Account a : list) {
			LinearLayout r = Views.row(this, pal, a.name, a.address, "…", false);
			r.setOnClickListener(v -> go(() -> folders(a)));
			r.setOnLongClickListener(v -> {
				forget(a);
				return true;
			});
			f[0].addView(r);
			f[0].addView(Views.rule(this, pal));
			work("", m -> m.inboxUnseen(a), n -> {
				TextView first = r.findViewWithTag("first");
				View top = (View) first.getParent();
				TextView right = (TextView) ((LinearLayout) top).getChildAt(1);
				right.setText(n == 0 ? "no unread" : n + " unread");
				if (n > 0) right.setTextColor(pal.accent);
			});
		}
		addButton(f[1], "Import from PC", true, v -> pick());
		addButton(f[1], "Add account", false, v -> go(this::add));
	}

	private void forget(Account a) {
		new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
			.setMessage("Remove " + a.name + " from this phone? Its mail stays on the server.")
			.setPositiveButton("Remove", (d, w) -> {
				try {
					store.remove(a.name);
				} catch (IOException e) {
					say(e.getMessage(), true);
				}
				again();
			})
			.setNegativeButton("Keep", null)
			.show();
	}

	/* ---------- folders, a page, a message ---------- */

	private void folders(Account a) {
		LinearLayout[] f = frame(a.name);
		addButton(f[1], "Write", true, v -> go(() -> compose(a, "", "", "", null)));
		work("Signing in to " + a.imapHost + "…", m -> m.folders(a), list -> {
			for (Imap.Folder fo : list) {
				LinearLayout r = Views.row(this, pal, fo.name, fo.selectable ? null : "a container: holds folders, no mail", null, false);
				if (fo.selectable) r.setOnClickListener(v -> go(() -> page(a, fo, 0)));
				else r.setAlpha(0.6f);
				f[0].addView(r);
				f[0].addView(Views.rule(this, pal));
			}
		});
	}

	private void page(Account a, Imap.Folder fo, long skip) {
		LinearLayout[] f = frame(a.name + " / " + fo.name);
		addButton(f[1], "Write", true, v -> go(() -> compose(a, "", "", "", null)));
		work("Reading " + fo.name + "…", m -> m.page(a, fo.raw, skip, PAGE), p -> {
			TextView top = Views.text(this, pal, p.exists + " messages, " + p.unseen + " unread"
				+ (p.exists > PAGE ? "  (" + (skip + 1) + "-" + Math.min(p.exists, skip + PAGE) + ")" : ""), 13, false, Palette.mix(pal.text, pal.bg, 30));
			int pad = Views.dp(this, 10);
			top.setPadding(pad, pad / 2, pad, pad / 2);
			f[0].addView(top);
			for (Imap.Header h : p.headers) {
				String from = h.from.replaceAll("\\s*<[^>]*>$", "");
				LinearLayout r = Views.row(this, pal, from.isEmpty() ? h.from : from, h.subject.isEmpty() ? "(no subject)" : h.subject,
					Views.when(h.date), !h.seen);
				r.setOnClickListener(v -> go(() -> read(a, fo, h.uid)));
				f[0].addView(r);
				f[0].addView(Views.rule(this, pal));
			}
			if (skip + PAGE < p.exists) addButton(f[1], "Older", false, v -> go(() -> page(a, fo, skip + PAGE)));
		});
	}

	private void read(Account a, Imap.Folder fo, long uid) {
		LinearLayout[] f = frame(a.name + " / " + fo.name);
		work("Opening…", m -> m.open(a, fo.raw, uid), l -> {
			int pad = Views.dp(this, 12);
			TextView subj = Views.text(this, pal, l.subject.isEmpty() ? "(no subject)" : l.subject, 17, true, pal.text);
			subj.setPadding(pad, pad, pad, pad / 2);
			f[0].addView(subj);
			TextView meta = Views.text(this, pal, "From  " + l.from + "\nTo    " + l.to + "\nDate  " + Views.when(l.date), 13, false,
				Palette.mix(pal.text, pal.bg, 25));
			meta.setPadding(pad, 0, pad, pad);
			meta.setTextIsSelectable(true);
			f[0].addView(meta);
			f[0].addView(Views.rule(this, pal));
			TextView body = Views.text(this, pal, l.text, 14, false, pal.text);
			body.setPadding(pad, pad, pad, pad);
			body.setTextIsSelectable(true);
			f[0].addView(body);
			addButton(f[1], "Reply", true, v -> go(() -> compose(a, Msg.bareAddr(l.from), Msg.reSubject(l.subject),
				"\n\n" + Msg.quoted(l.from, l.date, l.text), l.messageId.isEmpty() ? null : l.messageId)));
		});
	}

	/* ---------- writing ---------- */

	private void compose(Account a, String to, String subject, String body, String replyTo) {
		LinearLayout[] f = frame(replyTo == null ? "new message" : "reply");
		int pad = Views.dp(this, 8);
		f[0].setPadding(pad, pad, pad, pad);
		TextView from = Views.text(this, pal, "From  " + (a.display.isEmpty() ? a.address : a.display + " <" + a.address + ">"), 13, false,
			Palette.mix(pal.text, pal.bg, 25));
		from.setPadding(0, 0, 0, pad);
		f[0].addView(from);
		EditText t = Views.field(this, pal, "To", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
		EditText s = Views.field(this, pal, "Subject", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
		EditText b = Views.field(this, pal, "Message", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
		b.setMinLines(8);
		b.setGravity(Gravity.TOP);
		t.setText(to);
		s.setText(subject);
		b.setText(body);
		for (EditText e : new EditText[] {t, s, b}) {
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
			lp.setMargins(0, 0, 0, pad);
			f[0].addView(e, lp);
		}
		(to.isEmpty() ? t : b).requestFocus();
		addButton(f[1], "Send", true, v -> {
			String rcpt = t.getText().toString().trim();
			if (!Msg.addrOk(rcpt)) {
				say("To must be one plain mail address", true);
				return;
			}
			v.setEnabled(false);
			work("Sending through " + a.smtpHost + "…", m -> {
				m.send(a, rcpt, s.getText().toString(), b.getText().toString(), replyTo);
				return Boolean.TRUE;
			}, ok -> {
				onBackPressed();
				say("Sent to " + rcpt, false);
			});
			ui.postDelayed(() -> v.setEnabled(true), 1500);
		});
	}

	/* ---------- accounts in ---------- */

	/** an account by app password: Gmail (with 2-Step Verification on, myaccount.google.com/apppasswords) or IONOS */
	private void add() {
		LinearLayout[] f = frame("add account");
		int pad = Views.dp(this, 8);
		f[0].setPadding(pad, pad, pad, pad);
		final String[] preset = {"google"};
		LinearLayout pick = new LinearLayout(this);
		pick.setOrientation(LinearLayout.HORIZONTAL);
		TextView note = Views.text(this, pal, "", 13, false, Palette.mix(pal.text, pal.bg, 25));
		Runnable show = () -> note.setText("google".equals(preset[0])
			? "Gmail: use an app password (Google account, Security, 2-Step Verification, App passwords), or import the PC's Google sign-in instead."
			: "IONOS: the mailbox password.");
		TextView[] opts = new TextView[2];
		String[][] names = {{"google", "Gmail"}, {"ionos", "IONOS"}};
		for (int i = 0; i < 2; i++) {
			final int k = i;
			opts[i] = Views.button(this, pal, names[i][1], i == 0, null);
			opts[i].setOnClickListener(v -> {
				preset[0] = names[k][0];
				for (int j = 0; j < 2; j++) opts[j].setAlpha(j == k ? 1f : 0.5f);
				show.run();
			});
			opts[i].setAlpha(i == 0 ? 1f : 0.5f);
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
			lp.setMargins(0, 0, pad, pad);
			pick.addView(opts[i], lp);
		}
		show.run();
		f[0].addView(pick);
		note.setPadding(0, 0, 0, pad);
		f[0].addView(note);
		EditText name = Views.field(this, pal, "Name on this phone (e.g. gmail)", InputType.TYPE_CLASS_TEXT);
		EditText addr = Views.field(this, pal, "Mail address", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
		EditText disp = Views.field(this, pal, "Your name, as sent (optional)", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_WORDS);
		EditText pw = Views.field(this, pal, "Password or app password", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
		for (EditText e : new EditText[] {name, addr, disp, pw}) {
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
			lp.setMargins(0, 0, 0, pad);
			f[0].addView(e, lp);
		}
		addButton(f[1], "Sign in and save", true, v -> {
			Account a = Account.preset(preset[0], name.getText().toString().trim(), addr.getText().toString().trim(), false);
			a.display = disp.getText().toString().trim();
			String secret = "google".equals(preset[0]) ? pw.getText().toString().replace(" ", "") : pw.getText().toString();
			String why = a.problem();
			if (why == null && secret.isEmpty()) why = "the password is empty";
			if (why == null && store.account(a.name) != null) why = "there is already an account called " + a.name;
			if (why != null) {
				say(why, true);
				return;
			}
			keep(a, java.util.Collections.singletonMap("password", secret), null);
		});
	}

	/** tries the account with these secrets before anything is stored; on success saves it and goes home */
	private void keep(Account a, Map<String, String> secrets, Uri delete) {
		work("Signing in to " + a.imapHost + "…", m -> {
			Secrets trial = (acct, kind) -> secrets.get(kind);
			new Mail(trial, m.oauth, m.tls).folders(a);
			store.save(a, secrets);
			if (delete == null) return "";
			try {
				return DocumentsContract.deleteDocument(getContentResolver(), delete) ? " The file was deleted from the phone."
					: " Delete the file from Downloads: it holds the sign-in.";
			} catch (Exception e) {
				return " Delete the file from Downloads: it holds the sign-in.";
			}
		}, after -> {
			stack.clear();
			here = null;
			go(this::home);
			say(a.name + " added: " + a.address + "." + after, false);
		});
	}

	@SuppressWarnings("deprecation")
	private void pick() {
		Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
		i.addCategory(Intent.CATEGORY_OPENABLE);
		i.setType("*/*");
		startActivityForResult(i, REQ_IMPORT);
	}

	@Override
	@SuppressWarnings("deprecation")
	protected void onActivityResult(int req, int res, Intent data) {
		super.onActivityResult(req, res, data);
		if (req != REQ_IMPORT || res != RESULT_OK || data == null || data.getData() == null) return;
		Uri u = data.getData();
		Account.Import im;
		try (InputStream in = getContentResolver().openInputStream(u)) {
			if (in == null) throw new IOException("the file could not be opened");
			ByteArrayOutputStream o = new ByteArrayOutputStream();
			byte[] buf = new byte[8192];
			for (int n; (n = in.read(buf)) > 0;) {
				o.write(buf, 0, n);
				if (o.size() > 65536) throw new IOException("that file is too large to be an account file");
			}
			im = Account.fromExport(new String(o.toByteArray(), StandardCharsets.UTF_8));
		} catch (IOException | RuntimeException e) {
			say("Not imported: " + e.getMessage(), true);
			return;
		}
		if (store.account(im.account.name) != null) {
			say("Replacing " + im.account.name + " with the PC's copy.", false);
		}
		keep(im.account, im.secrets, u);
	}

	/** the stand-in build's first start: its one test account, from assets (test data only, never a real secret) */
	private void seedStandin() {
		try {
			Account.Import im = Account.fromExport(Env.asset(this, "standin-account.json"));
			store.save(im.account, im.secrets);
		} catch (IOException | RuntimeException ignored) {
		}
	}
}
