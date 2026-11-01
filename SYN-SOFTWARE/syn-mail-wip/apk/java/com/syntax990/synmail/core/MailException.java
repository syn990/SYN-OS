/* ------------------------------------------------------------------------
 *   A mail failure in plain words, fit to show on the screen: never a
 *   password, token or other secret in its message.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.IOException;

public class MailException extends IOException {
	private static final long serialVersionUID = 1L;

	public MailException(String why) {
		super(why);
	}
}
