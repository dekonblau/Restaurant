<?php

/**
 * @file
 * Contains all page callbacks, forms and theming functions for Feeds
 * administrative pages.
 */

/**
 * Introductory help for admin/structure/feeds/%feeds_importer page.
 */
function feeds_ui_edit_help() {
  return t('
    <p>
    You can create as many Feeds importer configurations as you would like to. Each can have a distinct purpose like letting your users aggregate RSS feeds or importing a CSV file for content migration. Here are a couple of things that are important to understand in order to get started with Feeds:
    </p>
    <ul>
    <li>
    Every importer configuration consists of basic settings, a fetcher, a parser and a processor and their settings.
    </li>
    <li>
    The <strong>basic settings</strong> define the general behavior of the importer. <strong>Fetchers</strong> are responsible for loading data, <strong>parsers</strong> for organizing it and <strong>processors</strong> for "doing stuff" with it, usually storing it.
    </li>
    <li>
    In Basic settings, you can <strong>attach an importer configuration to a content type</strong>. This is useful when many imports of a kind should be created, for example in an RSS aggregation scenario. If you don\'t attach a configuration to a content type, you can use it on the !import page.
    </li>
    <li>
    Imports can be <strong>scheduled periodically</strong> - see the periodic import select box in the Basic settings.
    </li>
    <li>
    Processors can have <strong>mappings</strong> in addition to settings. Mappings allow you to define what elements of a data feed should be mapped to what content fields on a granular level. For instance, you can specify that a feed item\'s author should be mapped to a node\'s body.
    </li>
    </ul>
    ', array('!import' => l(t('Import'), 'import')));
}

/**
 * Help text for mapping.
 */
function feeds_ui_mapping_help() {
  return t('
  <p>
  Define which elements of a single item of a feed (= <em>Sources</em>) map to which content pieces in Backdrop (= <em>Targets</em>). Make sure that at least one definition has a <em>Unique target</em>. A unique target means that a value for a target can only occur once. E. g. only one item with the URL <em>http://example.com/content/1</em> can exist.
  </p>
  ');
}

/**
 * Build overview of available configurations.
 */
function feeds_ui_overview_form($form, array &$form_state) {
  $form = $form['enabled'] = $form['disabled'] = array();
  $current_path = current_path();
  $options = array('destination' => $current_path);

  $form['#header'] = array(
    t('Name'),
    t('Description'),
    t('Form'),
    t('Enabled'),
    t('Operations'),
  );
  foreach (feeds_importer_load_all(TRUE) as $importer) {
    $importer_form = array();

    $importer_form['name']['#markup'] = check_plain($importer->config['name']);
    $importer_form['description']['#markup'] = check_plain($importer->config['description']);
    if (empty($importer->config['content_type'])) {
      if (!$importer->disabled) {
        $importer_form['attached']['#markup'] = l(check_plain($importer->config['name']), 'import/' . check_plain($importer->id));
      } else {
        $importer_form['attached']['#markup'] = check_plain($importer->config['name']);
      }
    }
    else {
      if (!$importer->disabled) {
        $importer_form['attached']['#markup'] = l(node_type_get_name($importer->config['content_type']), 'node/add/' . str_replace('_', '-', $importer->config['content_type']));
      }
      else {
        $importer_form['attached']['#markup'] = check_plain(node_type_get_name($importer->config['content_type']));
      }
    }

    $importer_form[$importer->id] = array(
      '#type' => 'checkbox',
      '#default_value' => !$importer->disabled,
    );

    $links['edit'] = array(
      'title' => t('Edit'),
      'href' => 'admin/structure/feeds/' . $importer->id,
      'query' => $options,
    );
    if (module_exists('config') && user_access('synchronize configuration')) {
      $links['export'] = array(
        'title' => t('Export'),
        'href' => 'admin/config/development/configuration/single/export',
        'query' => array(
          'group' => 'Feeds importers',
          'name' => 'feeds.feeds_importer.' . $importer->id,
        ),
      );
    }
    $links['clone'] = array(
      'title' => t('Clone'),
      'href' => 'admin/structure/feeds/' . $importer->id . '/clone',
      'query' => $options,
    );
    $links['delete'] = array(
      'title' => t('Delete'),
      'href' => 'admin/structure/feeds/' . $importer->id . '/delete',
      'query' => $options,
    );
    $importer_form['operations'] = array(
      '#type' => 'dropbutton',
      '#links' => $links,
    );

    if ($importer->disabled) {
      $form['disabled'][$importer->id] = $importer_form;
    }
    else {
      $form['enabled'][$importer->id] = $importer_form;
    }
  }
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );
  return $form;
}

/**
 * Submit handler for feeds_ui_overview_form().
 */
function feeds_ui_overview_form_submit($form, &$form_state) {

  $disabled = array();
  foreach (feeds_importer_load_all(TRUE) as $importer) {
    $disabled[$importer->id] = !$form_state['values'][$importer->id];
    config_set('feeds.feeds_importer.' . $importer->id, 'disabled', !$form_state['values'][$importer->id]);
  }
  config_set('feeds.settings', 'default_feeds_importer', $disabled);
  feeds_cache_clear();
}

/**
 * Create a new configuration.
 *
 * @param $form_state
 *   Form API form state array.
 * @param $from_importer
 *   FeedsImporter object. If given, form will create a new importer as a copy
 *   of $from_importer.
 */
function feeds_ui_create_form($form, &$form_state, $from_importer = NULL) {
  $form['#from_importer'] = $from_importer;
  $form['name'] = array(
    '#type' => 'textfield',
    '#title' => t('Name'),
    '#description' => t('A natural name for this configuration. Example: RSS Feed. You can always change this name later.'),
    '#required' => TRUE,
    '#maxlength' => 128,
  );
  $form['id'] = array(
    '#type' => 'machine_name',
    '#required' => TRUE,
    '#maxlength' => 128,
    '#machine_name' => array(
      'exists' => 'feeds_ui_importer_machine_name_exists',
    ),
  );
  $form['description'] = array(
    '#type' => 'textfield',
    '#title' => t('Description'),
    '#description' => t('A description of this configuration.'),
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Create'),
  );
  return $form;
}

/**
 * Validation callback for the importer machine name field.
 */
function feeds_ui_importer_machine_name_exists($id) {
  if (feeds_load_importer_object($id)) {
    return TRUE;
  }
}

/**
 * Validation handler for feeds_build_create_form().
 */
function feeds_ui_create_form_validate($form, &$form_state) {
  if (!empty($form_state['values']['id'])) {
    $importer = feeds_importer($form_state['values']['id']);
    $importer->configFormValidate($form_state['values']);
  }
}

/**
 * Submit handler for feeds_build_create_form().
 */
function feeds_ui_create_form_submit($form, &$form_state) {
  // Create feed.
  $importer = feeds_importer($form_state['values']['id']);
  // If from_importer is given, copy its configuration.
  if (!empty($form['#from_importer'])) {
    $importer->copy($form['#from_importer']);
  }
  // In any case, we want to set this configuration's title and description.
  $importer->addConfig($form_state['values']);
  $importer->save();

  // Set a message and redirect to settings form.
  if (empty($form['#from_importer'])) {
    backdrop_set_message(t('Your configuration has been created with default settings. If they do not fit your use case you can adjust them here.'));
  }
  else {
    backdrop_set_message(t('A clone of the @name configuration has been created.', array('@name' => $form['#from_importer']->config['name'])));
  }
  $form_state['redirect'] = 'admin/structure/feeds/' . $importer->id;
  feeds_cache_clear();
}

/**
 * Delete configuration form.
 */
function feeds_ui_delete_form($form, &$form_state, $importer) {
  $form['#importer'] = $importer->id;
  $title = t('Would you really like to delete the importer @importer?', array('@importer' => $importer->config['name']));
  $button_label = t('Delete');

  return confirm_form(
    $form,
    $title,
    'admin/structure/feeds',
    t('This action cannot be undone.'),
    $button_label
  );
}

/**
 * Submit handler for feeds_ui_delete_form().
 */
function feeds_ui_delete_form_submit($form, &$form_state) {
  $form_state['redirect'] = 'admin/structure/feeds';

  // Remove importer.
  feeds_importer($form['#importer'])->delete();

  // Clear cache, deleting a configuration may have an affect on menu tree.
  feeds_cache_clear();
}

/**
 * Edit feed configuration.
 */
function feeds_ui_edit_page(FeedsImporter $importer, $active = 'help', $plugin_key = '') {
  // Get plugins and configuration.
  $plugins = FeedsPlugin::all();

  $config = $importer->config;
  // Base path for changing the active container.
  $path = 'admin/structure/feeds/' . $importer->id;

  $active_container = array(
    'class' => array('active-container'),
    'actions' => array(l(t('Help'), $path)),
  );
  switch ($active) {
    case 'help':
      $active_container['title'] = t('Getting started');
      $active_container['body'] = '<div class="help feeds-admin-ui">' . feeds_ui_edit_help() . '</div>';
      unset($active_container['actions']);
      break;

    case 'fetcher':
    case 'parser':
    case 'processor':
      $active_container['title'] = t('Select a !plugin_type', array('!plugin_type' => $active));
      $active_container['body'] = backdrop_get_form('feeds_ui_plugin_form', $importer, $active);
      break;

    case 'settings':
      if (empty($plugin_key)) {
        $active_container['title'] = t('Basic settings');
        $active_container['body'] = feeds_get_form($importer, 'configForm');
      }
      // feeds_plugin() returns a correct result because feed has been
      // instantiated previously.
      elseif (in_array($plugin_key, array_keys($plugins)) && $plugin = feeds_plugin($plugin_key, $importer->id)) {
        $active_container['title'] = t('Settings for !plugin', array('!plugin' => $plugins[$plugin_key]['name']));
        $active_container['body'] = feeds_get_form($plugin, 'configForm');
      }
      break;

    case 'mapping':
      $processor_name = isset($plugins[$config['processor']['plugin_key']]['name']) ? $plugins[$config['processor']['plugin_key']]['name'] : $plugins['FeedsMissingPlugin']['name'];
      $active_container['title'] = t('Mapping for @processor', array('@processor' => $processor_name));
      $active_container['body'] = backdrop_get_form('feeds_ui_mapping_form', $importer);
      break;
    case 'preview':
      module_load_include('pages.inc', 'feeds');
      $active_container['title'] = t('Import preview');
      $active_container['body'] = backdrop_get_form('feeds_preview_form', $importer->id);
      break;
  }

  // Build config info.
  $config_info = $info = array();
  $info['class'] = array('config-set');

  // Basic information.
  $items = array();
  $items[] = t('Attached to: @type', array('@type' => $importer->config['content_type'] ? node_type_get_name($importer->config['content_type']) : t('[none]')));
  if ($importer->config['import_period'] == FEEDS_SCHEDULE_NEVER) {
    $import_period = t('off');
  }
  elseif ($importer->config['import_period'] == 0) {
    $import_period = t('as often as possible');
  }
  else {
    $import_period = t('every !interval', array('!interval' => format_interval($importer->config['import_period'])));
  }
  $items[] = t('Periodic import: !import_period', array('!import_period' => $import_period));
  $items[] = $importer->config['import_on_create'] ? t('Import on submission') : t('Do not import on submission');

  $info['title'] = t('Basic settings');
  $info['body'] = array(
    array(
      'body' => theme('item_list', array('items' => $items)),
      'actions' => array(l(t('Settings'), $path . '/settings')),
    ),
  );
  $config_info[] = $info;

  // Fetcher.
  $fetcher = isset($plugins[$config['fetcher']['plugin_key']]) ? $plugins[$config['fetcher']['plugin_key']] : $plugins['FeedsMissingPlugin'];
  $actions = array();
  if ($importer->fetcher->hasConfigForm()) {
    $actions = array(l(t('Settings'), $path . '/settings/' . $config['fetcher']['plugin_key']));
  }
  $info['title'] = t('Fetcher');
  $info['body'] = array(
    array(
      'title' => $fetcher['name'],
      'body' => $fetcher['description'],
      'actions' => $actions,
    ),
  );
  $info['actions'] = array(l(t('Change'), $path . '/fetcher'));
  $config_info[] = $info;

  // Parser.
  $parser = isset($plugins[$config['parser']['plugin_key']]) ? $plugins[$config['parser']['plugin_key']] : $plugins['FeedsMissingPlugin'];
  $actions = array();
  if ($importer->parser->hasConfigForm()) {
    $actions = array(l(t('Settings'), $path . '/settings/' . $config['parser']['plugin_key']));
  }
  $info['title'] = t('Parser');
  $info['body'] = array(
    array(
      'title' => $parser['name'],
      'body' => $parser['description'],
      'actions' => $actions,
    ),
  );
  $info['actions'] = array(l(t('Change'), $path . '/parser'));
  $config_info[] = $info;

  // Processor.
  $processor = isset($plugins[$config['processor']['plugin_key']]) ? $plugins[$config['processor']['plugin_key']] : $plugins['FeedsMissingPlugin'];
  $actions = array();
  if ($importer->processor->hasConfigForm()) {
    $actions[] = l(t('Settings'), $path . '/settings/' . $config['processor']['plugin_key']);
  }
  $actions[] = l(t('Mapping'), $path . '/mapping');
  $info['title'] = t('Processor');
  $info['body'] = array(
    array(
      'title' => $processor['name'],
      'body' => $processor['description'],
      'actions' => $actions,
    ),
  );
  $info['actions'] = array(l(t('Change'), $path . '/processor'));
  $config_info[] = $info;

  // Preview
  $info['title'] = t('Preview');
  $info['body'] = array(
    array(
      'body' => t('Show a preview of the import.'),
      'actions' => array(
        l(t('Import preview'), $path . '/preview'),
      ),
    ),
  );
  $info['actions'] = array();
  $config_info[] = $info;

  // Validate configuration.
  $errors = $importer->validateConfig();
  if (count($errors)) {
    $message = t('There are some issues with the importer configuration: !errors', array(
      '!errors' => theme('item_list', array('items' => $errors)),
    ));
    backdrop_set_message($message, 'warning');
  }

  return theme('feeds_ui_edit_page', array(
    'info' => $config_info,
    'active' => $active_container,
  ));
}

/**
 * Build a form of plugins to pick from.
 *
 * @param $form_state
 *   Form API form state array.
 * @param $importer
 *   FeedsImporter object.
 * @param $type
 *   Plugin type. One of 'fetcher', 'parser', 'processor'.
 *
 * @return
 *   A Form API form definition.
 */
function feeds_ui_plugin_form($form, &$form_state, $importer, $type) {
  $plugins = FeedsPlugin::byType($type);

  $form['#importer'] = $importer->id;
  $form['#plugin_type'] = $type;

  $importer_key = $importer->config[$type]['plugin_key'];

  foreach ($plugins as $key => $plugin) {

    $form['plugin_key'][$key] = array(
      '#type' => 'radio',
      '#parents' => array('plugin_key'),
      '#title' => check_plain($plugin['name']),
      '#description' => filter_xss(isset($plugin['help']) ? $plugin['help'] : $plugin['description']),
      '#return_value' => $key,
      '#default_value' => ($key == $importer_key) ? $key : '',
    );
  }
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );
  return $form;
}

/**
 * Submit handler for feeds_ui_plugin_form().
 */
function feeds_ui_plugin_form_submit($form, &$form_state) {
  // Set the plugin and save feed.
  $importer = feeds_importer($form['#importer']);
  $importer->setPlugin($form_state['values']['plugin_key']);
  $importer->save();
  backdrop_set_message(t('Changed @type plugin.', array('@type' => $form['#plugin_type'])));
}

/**
 * Theme feeds_ui_plugin_form().
 */
function theme_feeds_ui_plugin_form($variables) {
  $form = $variables['form'];
  $output = '';

  foreach (element_children($form['plugin_key']) as $key) {

    // Assemble container, render form elements.
    $container = array(
      'title' => $form['plugin_key'][$key]['#title'],
      'body' => isset($form['plugin_key'][$key]['#description']) ? $form['plugin_key'][$key]['#description'] : '',
    );
    $form['plugin_key'][$key]['#title'] = t('Select');
    $form['plugin_key'][$key]['#attributes']['class'] = array('feeds-ui-radio-link');
    unset($form['plugin_key'][$key]['#description']);
    $container['actions'] = array(backdrop_render($form['plugin_key'][$key]));

    $output .= theme('feeds_ui_container', array('container' => $container));
  }

  $output .= backdrop_render_children($form);
  return $output;
}

/**
 * Edit mapping.
 *
 * @todo Completely merge this into config form handling. This is just a
 *   shared form of configuration, most of the common functionality can live in
 *   FeedsProcessor, a flag can tell whether mapping is supported or not.
 */
function feeds_ui_mapping_form($form, &$form_state, $importer) {
  $form['#importer'] = $importer->id;
  $form['#mappings'] = $mappings = $importer->processor->getMappings();
  $form['#feeds_targets'] = $importer->processor->getMappingTargets();
  $form['#feeds_sources'] = $importer->parser->getMappingSources();
  $form['help']['#markup'] = feeds_ui_mapping_help();
  $form['#prefix'] = '<div id="feeds-ui-mapping-form-wrapper">';
  $form['#suffix'] = '</div>';

  // Show message when target configuration gets changed.
  if (!empty($form_state['mapping_settings'])) {
    $form['#mapping_settings'] = $form_state['mapping_settings'];
    $form['changed'] = array(
      '#theme_wrappers' => array('container'),
      '#attributes' => array('class' => array('messages', 'warning')),
      '#markup' => t('* Changes made to target configuration are stored temporarily. Click Save to make your changes permanent.'),
    );
  }

  // Get mapping sources from parsers and targets from processor, format them
  // for output.
  // Some parsers do not define mapping sources but let them define on the fly.
  if ($sources = $importer->parser->getMappingSources()) {
    $source_options = _feeds_ui_format_options($sources);
    foreach ($sources as $k => $source) {
      if (!empty($source['deprecated'])) {
        continue;
      }
      $legend['sources'][$k]['name']['#markup'] = empty($source['name']) ? $k : $source['name'];
      $legend['sources'][$k]['description']['#markup'] = empty($source['description']) ? '' : $source['description'];
    }
  }
  else {
    $legend['sources']['#markup'] = t('This parser supports free source definitions. Enter the name of the source field in lower case into the Source text field above.');
  }
  $targets = $importer->processor->getMappingTargets();
  $target_options = _feeds_ui_format_options($targets);
  $legend['targets'] = array();
  foreach ($targets as $k => $target) {
    if (!empty($target['deprecated'])) {
      continue;
    }
    $legend['targets'][$k]['name']['#markup'] = empty($target['name']) ? $k : $target['name'];
    $legend['targets'][$k]['description']['#markup'] = empty($target['description']) ? '' : $target['description'];
  }

  // Legend explaining source and target elements.
  $form['legendset'] = array(
    '#type' => 'fieldset',
    '#title' => t('Legend'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
    '#tree' => TRUE,
  );
  $form['legendset']['legend'] = $legend;

  // Add config forms and remove flags to mappings.
  $form['config'] = $form['remove_flags'] = $form['mapping_weight'] = array(
    '#tree' => TRUE,
  );
  if (is_array($mappings)) {

    $delta = count($mappings) + 2;

    foreach ($mappings as $i => $mapping) {
      if (isset($targets[$mapping['target']])) {
        $form['config'][$i] = feeds_ui_mapping_settings_form($form, $form_state, $i, $mapping, $targets[$mapping['target']]);
      }

      $form['remove_flags'][$i] = array(
        '#type' => 'checkbox',
        '#title' => t('Remove'),
        '#prefix' => '<div class="feeds-ui-checkbox-link">',
        '#suffix' => '</div>',
      );

      $form['mapping_weight'][$i] = array(
        '#type' => 'weight',
        '#title' => '',
        '#default_value' => $i,
        '#delta' => $delta,
        '#attributes' => array(
          'class' => array(
            'feeds-ui-mapping-weight',
          ),
        ),
      );
    }
  }

  if (isset($source_options)) {
    $form['source'] = array(
      '#type' => 'select',
      '#title' => t('Source'),
      '#title_display' => 'invisible',
      '#options' => $source_options,
      '#empty_option' => t('- Select a source -'),
      '#description' => t('An element from the feed.'),
    );
  }
  else {
    $form['source'] = array(
      '#type' => 'textfield',
      '#title' => t('Source'),
      '#title_display' => 'invisible',
      '#size' => 20,
      '#default_value' => '',
      '#description' => t('The name of source field.'),
    );
  }
  $form['target'] = array(
    '#type' => 'select',
    '#title' => t('Target'),
    '#title_display' => 'invisible',
    '#options' => $target_options,
    '#empty_option' => t('- Select a target -'),
    '#description' => t('The field that stores the data.'),
  );

  $form['actions'] = array('#type' => 'actions');
  $form['actions']['save'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );
  return $form;
}

/**
 * Per mapper configuration form that is a part of feeds_ui_mapping_form().
 */
function feeds_ui_mapping_settings_form($form, $form_state, $i, $mapping, $target) {
  $form_state += array(
    'mapping_settings_edit' => NULL,
    'mapping_settings' => array(),
  );

  $base_button = array(
    '#submit' => array('feeds_ui_mapping_form_multistep_submit'),
    '#ajax' => array(
      'callback' => 'feeds_ui_mapping_settings_form_callback',
      'wrapper' => 'feeds-ui-mapping-form-wrapper',
      'effect' => 'fade',
      'progress' => 'none',
    ),
    '#i' => $i,
  );

  if (isset($form_state['mapping_settings'][$i])) {
    $mapping = $form_state['mapping_settings'][$i] + $mapping;
  }

  if ($form_state['mapping_settings_edit'] === $i) {
    $settings_form = array();

    foreach ($target['form_callbacks'] as $callback) {
      $settings_form += call_user_func($callback, $mapping, $target, $form, $form_state);
    }

    // Merge in the optional unique form.
    $settings_form += feeds_ui_mapping_settings_optional_unique_form($mapping, $target, $form, $form_state);

    return array(
      '#type' => 'container',
      '#attributes' => array(
        'class' => array('feeds-target-config'),
      ),
      'settings' => $settings_form,
      'save_settings' => $base_button + array(
        '#type' => 'submit',
        '#name' => 'mapping_settings_update_' . $i,
        '#value' => t('Update'),
        '#op' => 'update',
      ),
      'cancel_settings' => $base_button + array(
        '#type' => 'submit',
        '#name' => 'mapping_settings_cancel_' . $i,
        '#value' => t('Cancel'),
        '#op' => 'cancel',
      ),
    );
  }
  else {
    // Build the summary.
    $summary = array();

    foreach ($target['summary_callbacks'] as $callback) {
      $summary[] = call_user_func($callback, $mapping, $target, $form, $form_state);
    }

    // Filter out empty summary values.
    $summary = implode('<br />', array_filter($summary));

    // Append the optional unique summary.
    if ($optional_unique_summary = feeds_ui_mapping_settings_optional_unique_summary($mapping, $target, $form, $form_state)) {
      $summary .= ' ' . $optional_unique_summary;
    }

    if ($summary) {
      return array(
        'summary' => array(
          '#prefix' => '<div>',
          '#markup' => $summary,
          '#suffix' => '</div>',
        ),
        'edit_settings' => $base_button + array(
          '#type' => 'image_button',
          '#name' => 'mapping_settings_edit_' . $i,
          '#src' => 'core/misc/configure.png',
          '#attributes' => array('alt' => t('Edit')),
          '#op' => 'edit',
        ),
      );
    }
  }
  return array();
}

/**
 * Submit callback for a per mapper configuration form. Switches between edit
 * and summary mode.
 */
function feeds_ui_mapping_form_multistep_submit($form, &$form_state) {
  $trigger = $form_state['triggering_element'];

  switch ($trigger['#op']) {
    case 'edit':
      $form_state['mapping_settings_edit'] = $trigger['#i'];
      break;

    case 'update':
      $values = $form_state['values']['config'][$trigger['#i']]['settings'];
      $form_state['mapping_settings'][$trigger['#i']] = $values;
      unset($form_state['mapping_settings_edit']);
      break;

    case 'cancel':
      unset($form_state['mapping_settings_edit']);
      break;
  }

  $form_state['rebuild'] = TRUE;
}

/**
 * AJAX callback that returns the whole feeds_ui_mapping_form().
 */
function feeds_ui_mapping_settings_form_callback($form, $form_state) {
  return $form;
}

/**
 * Validation handler for feeds_ui_mapping_form().
 */
function feeds_ui_mapping_form_validate($form, &$form_state) {
  if (!strlen($form_state['values']['source']) xor !strlen($form_state['values']['target'])) {

    // Check triggering_element here so we can react differently for ajax
    // submissions.
    switch ($form_state['triggering_element']['#name']) {

      // Regular form submission.
      case 'op':
        if (!strlen($form_state['values']['source'])) {
          form_error($form['source'], t('You must select a mapping source.'));
        }
        else {
          form_error($form['target'], t('You must select a mapping target.'));
        }
        break;

      // Be more relaxed on ajax submission.
      default:
        form_set_value($form['source'], '', $form_state);
        form_set_value($form['target'], '', $form_state);
        break;
    }
  }
}

/**
 * Submission handler for feeds_ui_mapping_form().
 */
function feeds_ui_mapping_form_submit($form, &$form_state) {
  $importer = feeds_importer($form['#importer']);
  $processor = $importer->processor;

  $form_state += array(
    'mapping_settings' => array(),
    'mapping_settings_edit' => NULL,
  );

  // If an item is in edit mode, prepare it for saving.
  if ($form_state['mapping_settings_edit'] !== NULL) {
    $values = $form_state['values']['config'][$form_state['mapping_settings_edit']]['settings'];
    $form_state['mapping_settings'][$form_state['mapping_settings_edit']] = $values;
  }

  // We may set some settings to mappings that we remove in the subsequent step,
  // that's fine.
  $mappings = $form['#mappings'];
  foreach ($form_state['mapping_settings'] as $k => $v) {
    $mappings[$k] = array(
      'source' => $mappings[$k]['source'],
      'target' => $mappings[$k]['target'],
    ) + $v;
  }

  if (!empty($form_state['values']['remove_flags'])) {
    $remove_flags = array_keys(array_filter($form_state['values']['remove_flags']));

    foreach ($remove_flags as $k) {
      unset($mappings[$k]);
      unset($form_state['values']['mapping_weight'][$k]);
      backdrop_set_message(t('Mapping has been removed.'), 'status', FALSE);
    }
  }

  // Keep our keys clean.
  $mappings = array_values($mappings);

  if (!empty($mappings)) {
    array_multisort($form_state['values']['mapping_weight'], $mappings);
  }

  $processor->addConfig(array('mappings' => $mappings));

  if (strlen($form_state['values']['source']) && strlen($form_state['values']['target'])) {
    try {
      $mappings = $processor->getMappings();
      $mappings[] = array(
        'source' => $form_state['values']['source'],
        'target' => $form_state['values']['target'],
        'unique' => FALSE,
      );
      $processor->addConfig(array('mappings' => $mappings));
      backdrop_set_message(t('Mapping has been added.'));
    }
    catch (Exception $e) {
      backdrop_set_message($e->getMessage(), 'error');
    }
  }

  $importer->save();
  backdrop_set_message(t('Your changes have been saved.'));
}

/**
 * Walk the result of FeedsParser::getMappingSources() or
 * FeedsProcessor::getMappingTargets() and format them into
 * a Form API options array.
 */
function _feeds_ui_format_options($options, $show_deprecated = FALSE) {
  $result = array();
  foreach ($options as $k => $v) {
    if (!$show_deprecated && is_array($v) && !empty($v['deprecated'])) {
      continue;
    }
    if (is_array($v) && !empty($v['name'])) {
      $result[$k] = $v['name'] . ' (' . $k . ')';
      if (!empty($v['deprecated'])) {
        $result[$k] .= ' - ' . t('DEPRECATED');
      }
    }
    elseif (is_array($v)) {
      $result[$k] = $k;
    }
    else {
      $result[$k] = $v;
    }
  }
  asort($result);
  return $result;
}

/**
 * Per mapping settings summary callback. Shows whether a mapping is used as
 * unique or not.
 */
function feeds_ui_mapping_settings_optional_unique_summary($mapping, $target, $form, $form_state) {
  if (!empty($target['optional_unique'])) {
    if ($mapping['unique']) {
      return t('Used as <strong>unique</strong>.');
    }
    else {
      return t('Not used as unique.');
    }
  }
}

/**
 * Per mapping settings form callback. Lets the user choose if a target is as
 * unique or not.
 */
function feeds_ui_mapping_settings_optional_unique_form($mapping, $target, $form, $form_state) {
  $settings_form = array();

  if (!empty($target['optional_unique'])) {
    $settings_form['unique'] = array(
      '#type' => 'checkbox',
      '#title' => t('Unique'),
      '#default_value' => !empty($mapping['unique']),
    );
  }

  return $settings_form;
}

/**
 * Theme feeds_ui_overview_form().
 */
function theme_feeds_ui_overview_form($variables) {
  $form = $variables['form'];
  backdrop_add_css(backdrop_get_path('module', 'feeds_ui') . '/feeds_ui.css');

  // Iterate through all importers and build a table.
  $rows = array();
  foreach (array('enabled', 'disabled') as $type) {
    if (isset($form[$type])) {
      foreach (element_children($form[$type]) as $id) {
        $row = array();
        foreach (element_children($form[$type][$id]) as $col) {
          $row[$col] = array(
            'data' => backdrop_render($form[$type][$id][$col]),
            'class' => array($type),
          );
        }
        $rows[] = array(
          'data' => $row,
          'class' => array($type),
        );
      }
    }
  }

  $output = theme('table', array(
    'header' => $form['#header'],
    'rows' => $rows,
    'attributes' => array('class' => array('feeds-admin-importers')),
    'empty' => t('No importers available.'),
  ));

  if (!empty($rows)) {
    $output .= backdrop_render_children($form);
  }

  return $output;
}

/**
 * Theme feeds_ui_edit_page().
 */
function theme_feeds_ui_edit_page($variables) {
  $config_info = $variables['info'];
  $active_container = $variables['active'];
  backdrop_add_css(backdrop_get_path('module', 'feeds_ui') . '/feeds_ui.css');

  // Outer wrapper.
  $output = '<div class="feeds-settings clearfix">';

  // Build left bar.
  $output .= '<div class="left-bar">';
  foreach ($config_info as $info) {
    $output .= theme('feeds_ui_container', array('container' => $info));
  }
  $output .= '</div>';

  // Build configuration space.
  $output .= '<div class="configuration">';
  $output .= '<div class="configuration-squeeze">';
  $output .= theme('feeds_ui_container', array('container' => $active_container));
  $output .= '</div>';
  $output .= '</div>';

  // Close the outer wrapper.
  $output .= '</div>';

  return $output;
}

/**
 * Render a simple container. A container can have a title, a description and
 * one or more actions. Recursive.
 *
 * @todo Replace with theme_fieldset or a wrapper to theme_fieldset?
 *
 * @param $variables
 *   An array containing an array at 'container'.
 *   A 'container' array may contain one or more of the following keys:
 *   array(
 *     'title' => 'the title',
 *     'body' => 'the body of the container, may also be an array of more
 *                containers or a renderable array.',
 *     'class' => array('the class of the container.'),
 *     'id' => 'the id of the container',
 *   );
 */
function theme_feeds_ui_container($variables) {
  $container = $variables['container'];

  $class = array_merge(array('feeds-container'), empty($container['class']) ? array('plain') : $container['class']);
  $id = empty($container['id']) ? '' : ' id="' . $container['id'] . '"';
  $output = '<div class="' . implode(' ', $class) . '"' . $id . '>';

  if (isset($container['actions']) && count($container['actions'])) {
    $output .= '<ul class="container-actions">';
    foreach ($container['actions'] as $action) {
      $output .= '<li>' . $action . '</li>';
    }
    $output .= '</ul>';
  }

  if (!empty($container['title'])) {
    $output .= '<h4 class="feeds-container-title">';
    $output .= $container['title'];
    $output .= '</h4>';
  }

  if (!empty($container['body'])) {
    $output .= '<div class="feeds-container-body">';
    if (is_array($container['body'])) {
      if (isset($container['body']['#type'])) {
        $output .= backdrop_render($container['body']);
      }
      else {
        foreach ($container['body'] as $c) {
          $output .= theme('feeds_ui_container', array('container' => $c));
        }
      }
    }
    else {
      $output .= $container['body'];
    }
    $output .= '</div>';
  }

  $output .= '</div>';
  return $output;
}

/**
 * Theme function for feeds_ui_mapping_form().
 */
function theme_feeds_ui_mapping_form($variables) {
  $form = $variables['form'];

  $targets = $form['#feeds_targets'];
  $targets = _feeds_ui_format_options($targets, TRUE);

  $sources = $form['#feeds_sources'];
  // Some parsers do not define source options.
  $sources = $sources ? _feeds_ui_format_options($sources, TRUE) : array();

  // Build the actual mapping table.
  $header = array(
    t('Source'),
    t('Target'),
    t('Target configuration'),
    '&nbsp;',
    t('Weight'),
  );
  $rows = array();
  if (is_array($form['#mappings'])) {
    foreach ($form['#mappings'] as $i => $mapping) {
      $source = isset($sources[$mapping['source']]) ? check_plain($sources[$mapping['source']]) : check_plain($mapping['source']);
      $target = isset($targets[$mapping['target']]) ? check_plain($targets[$mapping['target']]) : '<em>' . t('Missing') . '</em>';
      // Add indicator to target if target configuration changed.
      if (isset($form['#mapping_settings'][$i])) {
        $target .= '<span class="warning">*</span>';
      }
      $rows[] = array(
        'data' => array(
          $source,
          $target,
          backdrop_render($form['config'][$i]),
          backdrop_render($form['remove_flags'][$i]),
          backdrop_render($form['mapping_weight'][$i]),
        ),
        'class' => array('draggable', 'tabledrag-leaf'),
      );
    }
  }
  if (!count($rows)) {
    $rows[] = array(
      array(
        'colspan' => 5,
        'data' => t('No mappings defined.'),
      ),
    );
  }
  $rows[] = array(
    backdrop_render($form['source']),
    backdrop_render($form['target']),
    '',
    backdrop_render($form['add']),
    '',
  );
  $output = '';
  if (!empty($form['changed'])) {
    // This form element is only available if target configuration changed.
    $output .= backdrop_render($form['changed']);
  }
  $output .= '<div class="help feeds-admin-ui">' . backdrop_render($form['help']) . '</div>';
  $output .= theme('table', array('header' => $header, 'rows' => $rows, 'attributes' => array('id' => 'feeds-ui-mapping-overview')));

  // Build the help table that explains available sources.
  $legend = '';
  $rows = array();
  foreach (element_children($form['legendset']['legend']['sources']) as $k) {
    $rows[] = array(
      check_plain(backdrop_render($form['legendset']['legend']['sources'][$k]['name'])),
      check_plain(backdrop_render($form['legendset']['legend']['sources'][$k]['description'])),
    );
  }
  if (count($rows)) {
    $legend .= '<h4>' . t('Sources') . '</h4>';
    $legend .= theme('table', array('header' => array(t('Name'), t('Description')), 'rows' => $rows));
  }

  // Build the help table that explains available targets.
  $rows = array();
  foreach (element_children($form['legendset']['legend']['targets']) as $k) {
    $rows[] = array(
      check_plain(backdrop_render($form['legendset']['legend']['targets'][$k]['name']) . ' (' . $k . ')'),
      check_plain(backdrop_render($form['legendset']['legend']['targets'][$k]['description'])),
    );
  }
  $legend .= '<h4>' . t('Targets') . '</h4>';
  $legend .= theme('table', array('header' => array(t('Name'), t('Description')), 'rows' => $rows));

  // Stick tables into collapsible fieldset.
  $form['legendset']['legend'] = array(
    '#markup' => '<div>' . $legend . '</div>',
  );

  $output .= backdrop_render($form['legendset']);
  $output .= backdrop_render_children($form);

  backdrop_add_tabledrag('feeds-ui-mapping-overview', 'order', 'sibling', 'feeds-ui-mapping-weight');
  return $output;
}

/**
 * Page callback to import a Feeds importer.
 */
function feeds_ui_importer_import() {
  backdrop_goto('admin/config/development/configuration/single/import',
    array('query' => array('destination' => 'admin/structure/feeds'))
  );
}
