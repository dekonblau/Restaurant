<?php


/**
 * @file
 * Functions to handle the browser upload/download backup destination.
 */

/**
 * A destination type for browser upload/download.
 *
 * @ingroup backup_migrate_destinations
 */
class backup_migrate_destination_browser extends backup_migrate_destination {
  /**
   * Get a row of data to be used in a list of items of this type.
   */  
  function get_list_row() {
    // Return none as this type should not be displayed.
    return array();
  }
}

/**
 * A destination type for browser upload.
 *
 * @ingroup backup_migrate_destinations
 */
class backup_migrate_destination_browser_upload extends backup_migrate_destination_browser {
  var $supported_ops = array('restore');
  function __construct() {
    $params = array();
    $params['name'] = "Upload";
    $params['machine_name'] = 'upload';
    parent::__construct($params);
  }

  /**
   * File load destination callback.
   */
  function load_file($file_id) {
    if ($file = file_save_upload('backup_migrate_restore_upload')) {
      $out = new backup_file(array('filepath' => $file->uri));
      backup_migrate_temp_files_add($file->uri);
      return $out;
    }
    return NULL;
  }
}

/**
 * A destination type for browser download.
 *
 * @ingroup backup_migrate_destinations
 */
class backup_migrate_destination_browser_download extends backup_migrate_destination_browser {
  var $supported_ops = array('manual backup');
  // Browser downloads must always be the last destination as they must end the current process when they are done.
  var $weight = 1000;

  function __construct() {
    $params = array();
    $params['name'] = "Download";
    $params['machine_name'] = 'download';
    parent::__construct($params);
  }

  /**
   * File save destination callback.
   */
  function save_file($file, $settings) {
    require_once dirname(__FILE__) . '/files.inc';
    $file->transfer();
  }
}

