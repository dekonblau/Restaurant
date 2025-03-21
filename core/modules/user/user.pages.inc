<?php
/**
 * @file
 * User page callback file for the user module.
 */

/**
 * Menu callback; retrieve a JSON object containing autocomplete suggestions for existing users.
 */
function user_autocomplete($string = '') {
  $matches = array();
  if ($string) {
    $result = db_select('users')->fields('users', array('name'))->condition('name', db_like($string) . '%', 'LIKE')->range(0, 10)->execute();
    foreach ($result as $user) {
      $matches[$user->name] = check_plain($user->name);
    }
  }

  // Displayed as JSON by delivery callback backdrop_json_deliver().
  return $matches;
}

/**
 * Menu callback for reset password form.
 *
 * @since 1.23.1 Replaces user_pass() as menu callback.
 *
 * @see user_pass()
 */
function user_pass_page() {
  backdrop_set_title(t('Reset password'));
  $user_password_form = backdrop_get_form('user_pass');

  // If user is logged in, always display with tabs appearance.
  if (user_is_logged_in()) {
    $output = $user_password_form;
  }
  else {
    $output = user_login_page_wrapper($user_password_form);
  }

  return $output;
}

/**
 * Form builder; request a password reset.
 *
 * @ingroup forms
 * @see user_pass_validate()
 * @see user_pass_submit()
 */
function user_pass() {
  global $user;

  $form['name'] = array(
    '#type' => 'textfield',
    '#title' => t('Username or email address'),
    '#size' => 60,
    '#maxlength' => max(USERNAME_MAX_LENGTH, EMAIL_MAX_LENGTH),
    '#required' => TRUE,
    '#default_value' => isset($_GET['name']) ? $_GET['name'] : '',
    '#attributes' => array(
      // Add attributes to field to prevent spell-jacking and unwanted
      // automatic changes caused by the browser.
      'autocapitalize' => 'none',
      'autocorrect' => 'off',
      'spellcheck' => 'false',
    ),
  );

  // Allow logged in users to request this also.
  if ($user->uid > 0) {
    $form['name']['#type'] = 'value';
    $form['name']['#value'] = $user->mail;
    $form['mail'] = array(
      '#prefix' => '<p>',
      '#markup' => t('Password reset instructions will be mailed to %email. You must log out to use the password reset link in the email.', array('%email' => $user->mail)),
      '#suffix' => '</p>',
    );
  }
  $form['actions'] = array('#type' => 'actions');
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Reset password'));

  return $form;
}

/**
 * Form validation handler for user_pass().
 *
 * @see user_pass_submit()
 */
function user_pass_validate($form, &$form_state) {
  $config = config('user.flood');

  // Do not allow any password reset from the current IP if the limit has been
  // reached.
  if (!flood_is_allowed('pass_reset_ip', $config->get('flood_ip_limit'), $config->get('flood_ip_window'))) {
    form_set_error('name', t('Sorry, too many password reset attempts from your IP address. Try again later.'));
    return;
  }
  // Always register a per-IP event.
  flood_register_event('pass_reset_ip', $config->get('flood_ip_window'));

  $name = trim($form_state['values']['name']);
  // Try to load by email.
  $users = user_load_multiple(array(), array('mail' => $name, 'status' => '1'));
  $account = reset($users);
  if (!$account) {
    // No success, try to load by name.
    $users = user_load_multiple(array(), array('name' => $name, 'status' => '1'));
    $account = reset($users);
  }
  if (isset($account->uid)) {
    // Register user flood events based on the uid only, so they can be cleared
    // when a password is reset successfully.
    $identifier = $account->uid;
    // Don't allow password reset if the limit for this user has been reached.
    // Default is to allow 5 passwords resets every 6 hours.
    if (!flood_is_allowed('pass_reset_user', $config->get('flood_user_limit'), $config->get('flood_user_window'), $identifier)) {
      form_set_error('name', t('Sorry, too many password reset attempts for this account. Try again later.'));
      return;
    }
    // Register a per-user event.
    flood_register_event('pass_reset_user', $config->get('flood_user_window'), $identifier);

    form_set_value(array('#parents' => array('account')), $account, $form_state);
  }
  else {
    form_set_error('name', t('Sorry, %name is not recognized as a user name or an email address.', array('%name' => $name)));
  }
}

/**
 * Form submission handler for user_pass().
 *
 * @see user_pass_validate()
 */
function user_pass_submit($form, &$form_state) {
  global $language;

  $account = $form_state['values']['account'];
  // Mail one time login URL and instructions using current language.
  $mail = _user_mail_notify('password_reset', $account, $language);
  if (!empty($mail)) {
    watchdog('user', 'Password reset instructions mailed to %name at %email.', array('%name' => $account->name, '%email' => $account->mail));
    backdrop_set_message(t('Further instructions have been sent to your email address.'));
  }

  $form_state['redirect'] = 'user';
  return;
}

/**
 * Menu callback; process one time login link and redirects to the user page on
 * success.
 *
 * @param int $uid
 *   User ID for the user who would like their password reset.
 * @param int $timestamp
 *   Timestamp when the one-time password reset link was generated.
 * @param string $hashed_pass
 *   Hashed version of the user's password using user_pass_rehash().
 * @param bool $immediate_login
 *   Skip the change password form and just immediately log in.
 */
function user_pass_reset($uid, $timestamp, $hashed_pass, $immediate_login = FALSE) {
  global $user;

  // When processing the one-time login link, we have to make sure that a user
  // isn't already logged in.
  if ($user->uid) {
    // The existing user is already logged in. Log them out and reload the
    // current page so the password reset process can continue.
    if ($user->uid == $uid) {
      // Preserve the current destination (if any) and ensure the redirect goes
      // back to the current page; any custom destination set in
      // hook_user_logout() and intended for regular logouts would not be
      // appropriate here.
      $destination = array();
      if (isset($_GET['destination'])) {
        $destination = backdrop_get_destination();
      }
      user_logout_current_user();
      unset($_GET['destination']);
      backdrop_goto(current_path(), array('query' => backdrop_get_query_parameters() + $destination));
    }
    // A different user is already logged in on the computer.
    else {
      $reset_link_account = user_load($uid);
      $is_valid = FALSE;
      if (!empty($reset_link_account) && $timestamp >= $reset_link_account->login && $timestamp <= REQUEST_TIME) {
        if ($hashed_pass == user_pass_rehash($reset_link_account->pass, $timestamp, $reset_link_account->login, $reset_link_account->uid, $reset_link_account->mail)) {
          $is_valid = TRUE;
        }
      }
      if ($is_valid) {
        backdrop_set_message(t('You cannot use a password reset link while logged into the site. Please <a href="!logout">logout</a> and try using the link again.',
          array('!logout' => url('user/logout'))), 'warning');
      } else {
        // Invalid one-time link specifies an unknown user.
        backdrop_set_message(t('The one-time login link you clicked is invalid.'), 'error');
      }
      backdrop_goto();
    }
  }
  else {
    // Time out, in seconds, until login URL expires.
    $timeout = config_get('system.core', 'user_password_reset_timeout');
    $current = REQUEST_TIME;
    // Some redundant checks for extra security?
    $users = user_load_multiple(array($uid), array('status' => '1'));
    if ($timestamp <= $current && $account = reset($users)) {
      // No time out for first time login.
      if ($account->login && $current - $timestamp > $timeout) {
        backdrop_set_message(t('You have tried to use a reset password link that has expired. Please request a new one using the form below.'), 'error');
        backdrop_goto('user/password');
      }
      elseif ($account->uid && $timestamp >= $account->login && $timestamp <= $current && $hashed_pass == user_pass_rehash($account->pass, $timestamp, $account->login, $account->uid, $account->mail)) {
        if (config_get('user.flood', 'flood_uid_only')) {
          // Clear flood events based on the uid only if configured.
          $identifier = $account->uid;
        }
        else {
          // The default identifier is a combination of uid and IP address.
          $identifier = $account->uid . '-' . ip_address();
        }
        // Only clear the user specific flood events. We cannot clear the more
        // broad IP address flood events because that would open a
        // vulnerability where an attacker with a valid account could use that
        // to brute force other accounts.
        flood_clear_event('failed_login_attempt_user', $identifier);

        if ($immediate_login) {
          $user = $account;
          user_login_finalize();
          // Clear any password reset flood events for this user.
          flood_clear_event('pass_reset_user', $account->uid);
          // Clear any failed password validation flood events for this user.
          flood_clear_event('failed_pass_validation_user', $account->uid);
          backdrop_set_message(t('You have used your one-time log-in link and are now logged-in.'));
          watchdog('user', 'User %name used one-time password reset link at time %timestamp.', array('%name' => $account->name, '%timestamp' => format_date(REQUEST_TIME, 'long')));
          backdrop_goto();
        }
        else {
          return backdrop_get_form('user_pass_reset_form', $account);
        }
      }
      else {
        backdrop_set_message(t('You have tried to use a reset password link that has either been used or is no longer valid. Please request a new one using the form below.'), 'error');
        backdrop_goto('user/password');
      }
    }
    else {
      // Deny access, no more clues.
      // Everything will be in the watchdog's URL for the administrator to check.
      backdrop_access_denied();
      backdrop_exit();
    }
  }
}

/**
 * Form builder; one time login form with password reset.
 *
 * @ingroup forms
 * @see user_pass_reset_form_submit()
 */
function user_pass_reset_form($form, &$form_state, $account) {
  $form['#account'] = $account;
  $description = '';
  module_load_include('password.inc', 'user', 'user');
  $reject_weak = user_password_reject_weak($account->name);
  if ($reject_weak) {
    $form['#validate'][] = 'user_password_policy_validate';
    $description = _user_password_policy_help();
  }
  $form['message'] = array(
    '#markup' => t('<p>Please enter a new password to access your account.</p>'),
  );
  $form['pass'] = array(
    '#type' => 'password_confirm',
    '#required' => TRUE,
    '#description' => $description,
  );
  $form['actions'] = array(
    '#type' => 'actions',
  );
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save password & log in'),
  );

  return $form;
}

/**
 * Submit handler for user_pass_reset_form().
 */
function user_pass_reset_form_submit($form, &$form_state) {
  global $user;
  $user = $form['#account'];
  $user->pass = $form_state['values']['pass'];
  $user->save();
  // user_login_finalize() also updates the login timestamp of the user, which
  // invalidates further use of the one-time login link.
  user_login_finalize();
  // Clear any password reset flood events for this user.
  flood_clear_event('pass_reset_user', $user->uid);
  backdrop_set_message(t('Your account password has been updated.'));
  watchdog('user', 'User %name used one-time password reset link at time %timestamp.', array('%name' => $form['#account']->name, '%timestamp' => format_date(REQUEST_TIME, 'long')));
  backdrop_goto();
}

/**
 * Menu callback; logs the current user out, and redirects to the home page.
 */
function user_logout() {
  user_logout_current_user();
  backdrop_goto();
}

/**
 * Logs the current user out.
 */
function user_logout_current_user() {
  global $user;

  watchdog('user', 'Session closed for %name.', array('%name' => $user->name));

  module_invoke_all('user_logout', $user);

  $entity_info = entity_get_info('user');
  if (isset($entity_info['entity cache']) && $entity_info['entity cache']) {
    cache('entity_user')->delete($user->uid);
  }

  // Destroy the current session, and reset $user to the anonymous user.
  session_destroy();
}

/**
 * Form builder; edit a user account.
 *
 * @ingroup forms
 * @see user_account_form()
 * @see user_account_form_validate()
 * @see user_profile_form_validate()
 * @see user_profile_form_submit()
 * @see user_cancel_confirm_form_submit()
 */
function user_profile_form($form, &$form_state, $account) {
  global $user;

  // During initial form build, add the entity to the form state for use during
  // form building and processing. During a rebuild, use what is in the form
  // state.
  if (!isset($form_state['user'])) {
    $form_state['user'] = $account;
  }
  else {
    $account = $form_state['user'];
  }

  // @todo Legacy support. Modules are encouraged to access the entity using
  //   $form_state.
  $form['#user'] = $account;


  user_account_form($form, $form_state);
  // Attach field widgets.
  field_attach_form('user', $account, $form, $form_state);

  // Prepare cancel link.
  if (isset($_GET['destination'])) {
    $path = $_GET['destination'];
  }
  elseif (isset($_SERVER['HTTP_REFERER'])) {
    $path = $_SERVER['HTTP_REFERER'];
  }
  elseif (isset($account->uid)) {
    $path = 'user/' . $account->uid;
  }
  else {
    $path = '<front>';
  }
  $options = backdrop_parse_url($path);
  $options['attributes']['class'][] = 'form-cancel';

  $form['actions'] = array('#type' => 'actions');
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );
  $form['actions']['cancel'] = array(
    '#type' => 'submit',
    '#value' => t('Cancel account'),
    '#submit' => array('user_edit_cancel_submit'),
    '#access' => $account->uid > 1 && (($account->uid == $user->uid && user_access('cancel account')) || user_access('administer users')),
  );
  $form['actions']['cancel_form'] = array(
    '#type' => 'link',
    '#title' => t('Cancel'),
    '#href' => $options['path'],
    '#options' => $options,
    '#weight' => 1,
  );

  $form['#validate'][] = 'user_profile_form_validate';
  // Add the final user profile form submit handler.
  $form['#submit'][] = 'user_profile_form_submit';

  return $form;
}

/**
 * Form validation handler for user_profile_form().
 *
 * @see user_profile_form_submit()
 */
function user_profile_form_validate($form, &$form_state) {
  entity_form_field_validate('user', $form, $form_state);
}

/**
 * Form submission handler for user_profile_form().
 *
 * @see user_profile_form_validate()
 */
function user_profile_form_submit($form, &$form_state) {
  $account = $form_state['user'];
  // Remove unneeded values.
  form_state_values_clean($form_state);

  // Convert checkbox values to an unindexed list.
  $form_state['values']['roles'] = array_keys(array_filter($form_state['values']['roles']));

  entity_form_submit_build_entity('user', $account, $form, $form_state);
  $account->save();
  $form_state['values']['uid'] = $account->uid;

  if (strlen(trim($form_state['values']['pass'])) > 0) {
    // Remove the password reset tag since a new password was saved.
    unset($_SESSION['pass_reset_'. $account->uid]);
  }

  // Redirect to view the user account.
  $form_state['redirect'] = 'user/' . $account->uid;

  backdrop_set_message(t('The changes have been saved.'));
}

/**
 * Submit handler for user_edit_cancel().
 */
function user_edit_cancel_submit($form, &$form_state) {
  $destination = array();
  if (isset($_GET['destination'])) {
    $destination = backdrop_get_destination();
    unset($_GET['destination']);
  }
  // Note: We redirect from user/uid/edit to user/uid/cancel to make the tabs disappear.
  $form_state['redirect'] = array("user/" . $form['#user']->uid . "/cancel", array('query' => $destination));
}

/**
 * Form builder; confirm form for canceling user account.
 *
 * @ingroup forms
 * @see user_edit_cancel_submit()
 * @see user_cancel_confirm_form_submit()
 */
function user_cancel_confirm_form($form, &$form_state, $account) {
  global $user;

  $form['_account'] = array('#type' => 'value', '#value' => $account);

  // Display account cancellation method selection, if allowed.
  $admin_access = user_access('administer users');
  $can_select_method = $admin_access || user_access('select account cancellation method');
  $form['user_cancel_method'] = array(
    '#type' => 'item',
    '#title' => ($account->uid == $user->uid ? t('When cancelling your account') : t('When cancelling the account')),
    '#access' => $can_select_method,
  );
  $form['user_cancel_method'] += user_cancel_methods();

  // Allow user administrators to skip the account cancellation confirmation
  // mail (by default), as long as they do not attempt to cancel their own
  // account.
  $override_access = $admin_access && ($account->uid != $user->uid);
  $form['user_cancel_confirm'] = array(
    '#type' => 'checkbox',
    '#title' => t('Require email confirmation to cancel account.'),
    '#default_value' => ($override_access ? FALSE : TRUE),
    '#access' => $override_access,
    '#description' => t('When enabled, the user must confirm the account cancellation via email.'),
  );
  // Also allow to send account canceled notification mail, if enabled.
  $default_notify = config_get('system.core', 'user_mail_status_canceled_notify');
  $form['user_cancel_notify'] = array(
    '#type' => 'checkbox',
    '#title' => t('Notify user when account is canceled.'),
    '#default_value' => ($override_access ? FALSE : $default_notify),
    '#access' => $override_access && $default_notify,
    '#description' => t('When enabled, the user will receive an email notification after the account has been cancelled.'),
  );

  // Prepare confirmation form page title and description.
  if ($account->uid == $user->uid) {
    $question = t('Are you sure you want to cancel your account?');
  }
  else {
    $question = t('Are you sure you want to cancel the account %name?', array('%name' => $account->name));
  }
  $description = '';
  if ($can_select_method) {
    $description = t('Select the method to cancel the account above.');
    foreach (element_children($form['user_cancel_method']) as $element) {
      unset($form['user_cancel_method'][$element]['#description']);
    }
  }
  else {
    // The radio button #description is used as description for the confirmation
    // form.
    foreach (element_children($form['user_cancel_method']) as $element) {
      if ($form['user_cancel_method'][$element]['#default_value'] == $form['user_cancel_method'][$element]['#return_value']) {
        $description = $form['user_cancel_method'][$element]['#description'];
      }
      unset($form['user_cancel_method'][$element]['#description']);
    }
  }

  // Always provide entity id in the same form key as in the entity edit form.
  $form['uid'] = array('#type' => 'value', '#value' => $account->uid);
  return confirm_form($form,
    $question,
    'user/' . $account->uid,
    $description . ' ' . t('This action cannot be undone.'),
    t('Cancel account'), t('Cancel'));
}

/**
 * Submit handler for user_cancel_confirm_form().
 *
 * @see user_multiple_cancel_confirm_submit()
 */
function user_cancel_confirm_form_submit($form, &$form_state) {
  global $user;
  $account = $form_state['values']['_account'];

  // Cancel account immediately, if the current user has administrative
  // privileges, no confirmation mail shall be sent, and the user does not
  // attempt to cancel the own account.
  if (user_access('administer users') && empty($form_state['values']['user_cancel_confirm']) && $account->uid != $user->uid) {
    user_cancel($form_state['values'], $account->uid, $form_state['values']['user_cancel_method']);

    $form_state['redirect'] = 'admin/people';
  }
  else {
    // Store cancelling method and whether to notify the user in $account for
    // user_cancel_confirm().
    $account->user_cancel_method = $form_state['values']['user_cancel_method'];
    $account->user_cancel_notify = $form_state['values']['user_cancel_notify'];
    $account->save();
    _user_mail_notify('cancel_confirm', $account);
    backdrop_set_message(t('A confirmation request to cancel your account has been sent to your email address.'));
    watchdog('user', 'Sent account cancellation request to %name %email.', array('%name' => $account->name, '%email' => '<' . $account->mail . '>'), WATCHDOG_NOTICE);

    $form_state['redirect'] = "user/$account->uid";
  }
}

/**
 * Helper function to return available account cancellation methods.
 *
 * @return
 *   An array containing all account cancellation methods as form elements.
 *
 * @see hook_user_cancel_methods_alter()
 * @see user_admin_settings()
 * @see user_cancel_confirm_form()
 * @see user_multiple_cancel_confirm()
 */
function user_cancel_methods() {
  $site_config = config('system.core');
  $anonymous_name = $site_config->get('anonymous');
  $methods = array(
    'user_cancel_block' => array(
      'title' => t('Disable the account and keep its content'),
      'description' => t('Your account will be blocked and you will no longer be able to log in. All of your content will remain attributed to your user name.'),
    ),
    'user_cancel_block_unpublish' => array(
      'title' => t('Disable the account and unpublish its content'),
      'description' => t('Your account will be blocked and you will no longer be able to log in. All of your content will be hidden from everyone but administrators.'),
    ),
    'user_cancel_reassign' => array(
      'title' => t('Delete the account and make its content belong to the %anonymous-name user', array('%anonymous-name' => $anonymous_name)),
      'description' => t('Your account will be removed and all account information deleted. All of your content will be assigned to the %anonymous-name user.', array('%anonymous-name' => $anonymous_name)),
    ),
    'user_cancel_delete' => array(
      'title' => t('Delete the account and its content'),
      'description' => t('Your account will be removed and all account information deleted. All of your content will also be deleted.'),
      'access' => user_access('administer users'),
    ),
  );
  // Allow modules to customize account cancellation methods.
  backdrop_alter('user_cancel_methods', $methods);

  // Turn all methods into real form elements.
  $default_method = $site_config->get('user_cancel_method');
  foreach ($methods as $name => $method) {
    $form[$name] = array(
      '#type' => 'radio',
      '#title' => $method['title'],
      '#description' => (isset($method['description']) ? $method['description'] : NULL),
      '#return_value' => $name,
      '#default_value' => $default_method,
      '#parents' => array('user_cancel_method'),
    );
  }
  return $form;
}

/**
 * Menu callback; cancel a user account via email confirmation link.
 *
 * @see user_cancel_confirm_form()
 * @see user_cancel_url()
 */
function user_cancel_confirm($account, $timestamp = 0, $hashed_pass = '') {
  // Time out in seconds until cancel URL expires; 24 hours = 86400 seconds.
  $timeout = 86400;
  $current = REQUEST_TIME;

  // Basic validation of arguments.
  if (isset($account->data['user_cancel_method']) && !empty($timestamp) && !empty($hashed_pass)) {
    // Validate expiration and hashed password/login.
    if ($timestamp <= $current && $current - $timestamp < $timeout && $account->uid && $timestamp >= $account->login && $hashed_pass == user_pass_rehash($account->pass, $timestamp, $account->login, $account->uid, $account->mail)) {
      $edit = array(
        'user_cancel_notify' => isset($account->data['user_cancel_notify']) ? $account->data['user_cancel_notify'] : config_get('system.core', 'user_mail_status_canceled_notify'),
      );
      user_cancel($edit, $account->uid, $account->data['user_cancel_method']);
      // Since user_cancel() is not invoked via Form API, batch processing needs
      // to be invoked manually and should redirect to the home page after
      // completion.
      batch_process('');
    }
    else {
      backdrop_set_message(t('You have tried to use an account cancellation link that has expired. Please request a new one using the form below.'), 'error');
      backdrop_goto("user/$account->uid/cancel");
    }
  }
  return MENU_ACCESS_DENIED;
}

/**
 * Page callback: Displays the user page.
 *
 * Displays user profile if user is logged in, or login form for anonymous
 * users.
 *
 * @return
 *   A render array for either a user profile or a login form.
 *
 * @see user_view_page()
 * @see user_login()
 */
function user_page() {
  global $user;

  // To help users upgrading from versions of Backdrop prior to 1.9.0 where this
  // menu callback was used on 'user/login' as well as 'user', rebuild the
  // menu automatically if this page is loaded at 'user/login'.
  // See https://github.com/backdrop/backdrop-issues/issues/260.
  if ($_GET['q'] === 'user/login') {
    menu_rebuild();
  }

  if ($user->uid) {
    backdrop_goto('user/' . $user->uid);
  }
  else {
    backdrop_goto('user/login');
  }
}
