/* ------------------------------------------------------------------------
 *   Where an account's secrets are kept. On the phone: app-private, sealed
 *   with an Android Keystore key (Store). In the tests: a map. Kinds, as on
 *   the desktop: password, refresh_token, client_id, client_secret.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL-APK
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
package com.syntax990.synmail.core;

import java.io.IOException;

public interface Secrets {
	/** the secret, or null if there is none */
	String get(String account, String kind) throws IOException;
}
