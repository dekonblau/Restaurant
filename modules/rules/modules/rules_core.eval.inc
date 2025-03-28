<?php
/**
 * @file
 * Contains rules core integration needed during evaluation.
 *
 * @addtogroup rules
 *
 * @{
 */

/**
 * Action and condition callback: Invokes a rules component.
 *
 * We do not use the execute() method, but handle executing ourself. That way
 * we can utilize the existing state for saving passed variables.
 */
function rules_element_invoke_component($arguments, RulesPlugin $element) {
  $info = $element->info();
  $state = $arguments['state'];
  $wrapped_args = $state->currentArguments;

  if ($component = rules_get_cache('comp_' . $info['#config_name'])) {
    $replacements = array('%label' => $component->label(), '@plugin' => $component->plugin());
    // Handle recursion prevention.
    if ($state->isBlocked($component)) {
      return rules_log('Not evaluating @plugin %label to prevent recursion.', $replacements, RulesLog::INFO, $component);
    }
    $state->block($component);
    rules_log('Evaluating @plugin %label.', $replacements, RulesLog::INFO, $component, TRUE);
    module_invoke_all('rules_config_execute', $component);

    // Manually create a new evaluation state and evaluate the component.
    $args = array_intersect_key($wrapped_args, $component->parameterInfo());
    $new_state = $component->setUpState($wrapped_args);
    $return = $component->evaluate($new_state);

    // Care for the right return value in case we have to provide vars.
    if ($component instanceof RulesActionInterface && !empty($info['provides'])) {
      $return = array();
      foreach ($info['provides'] as $var => $var_info) {
        $return[$var] = $new_state->get($var);
      }
    }

    // Now merge the info about to be saved variables in the parent state.
    $state->mergeSaveVariables($new_state, $component, $element->settings);
    $state->unblock($component);

    // Cleanup the state, what saves not mergeable variables now.
    $new_state->cleanup();
    rules_log('Finished evaluation of @plugin %label.', $replacements, RulesLog::INFO, $component, FALSE);
    return $return;
  }
  else {
    throw new RulesEvaluationException('Unable to get the component %name', array(
      '%name' => $info['#config_name'],
    ), $element, RulesLog::ERROR);
  }
}

/**
 * A class implementing a rules input evaluator processing date input.
 *
 * This is needed to treat relative date inputs for strtotime() correctly.
 * Consider for example "now".
 */
class RulesDateInputEvaluator extends RulesDataInputEvaluator {

  const DATE_REGEX_LOOSE = '/^(\d{4})-?(\d{2})-?(\d{2})([T\s]?(\d{2}):?(\d{2}):?(\d{2})?)?$/';

  /**
   * Overrides RulesDataInputEvaluator::prepare().
   */
  public function prepare($text, $var_info) {
    if (is_numeric($text)) {
      // Let rules skip this input evaluators in case it's already a timestamp.
      $this->setting = NULL;
    }
  }

  /**
   * Overrides RulesDataInputEvaluator::evaluate().
   */
  public function evaluate($text, $options, RulesState $state) {
    return self::gmstrtotime($text);
  }

  /**
   * Convert a time string to a GMT (UTC) unix timestamp.
   */
  public static function gmstrtotime($date) {
    // Fixed date strings are evaluated in GMT (UTC) so save and restore the
    // current default time zone. Pass the current timestamp in UTC to ensure
    // the retrieved time is UTC.
    $timezone = date_default_timezone_get();
    date_default_timezone_set('UTC');
    $time = strtotime($date, time());
    date_default_timezone_set($timezone);
    return $time;
  }

  /**
   * Determine whether the given date string specifies a fixed date.
   */
  public static function isFixedDateString($date) {
    return is_string($date) && preg_match(self::DATE_REGEX_LOOSE, $date);
  }

}

/**
 * A class implementing a rules input evaluator processing URI inputs.
 *
 * Makes sure URIs are absolute and path aliases get applied.
 */
class RulesURIInputEvaluator extends RulesDataInputEvaluator {

  /**
   * Overrides RulesDataInputEvaluator::prepare().
   */
  public function prepare($uri, $var_info) {
    if (!isset($this->processor) && valid_url($uri, TRUE)) {
      // Only process if another evaluator is used or the url is not absolute.
      $this->setting = NULL;
    }
  }

  /**
   * Overrides RulesDataInputEvaluator::evaluate().
   */
  public function evaluate($uri, $options, RulesState $state) {
    if (!url_is_external($uri)) {
      // Extract the path and build the URL using the url() function, so URL
      // aliases are applied and query parameters and fragments get handled.
      $url = backdrop_parse_url($uri);
      $url_options = array('absolute' => TRUE);
      $url_options['query'] = $url['query'];
      $url_options['fragment'] = $url['fragment'];
      return url($url['path'], $url_options);
    }
    elseif (valid_url($uri)) {
      return $uri;
    }
    throw new RulesEvaluationException('Input evaluation generated an invalid URI.', array(), NULL, RulesLog::WARN);
  }

}

/**
 * A data processor for applying date offsets.
 */
class RulesDateOffsetProcessor extends RulesDataProcessor {

  /**
   * Overrides RulesDataProcessor::form().
   */
  protected static function form($settings, $var_info) {
    $settings += array('value' => '');
    $form = array(
      '#type' => 'fieldset',
      '#title' => t('Add offset'),
      '#collapsible' => TRUE,
      '#collapsed' => empty($settings['value']),
      '#description' => t('Add an offset to the selected date.'),
    );
    $form['value'] = array(
      '#type' => 'rules_duration',
      '#title' => t('Offset'),
      '#description' => t('Note that you can also specify negative numbers.'),
      '#default_value' => $settings['value'],
      '#weight' => 5,
    );
    return $form;
  }

  /**
   * Overrides RulesDataProcessor::process().
   */
  public function process($value, $info, RulesState $state, RulesPlugin $element) {
    $value = isset($this->processor) ? $this->processor->process($value, $info, $state, $element) : $value;
    return RulesDateOffsetProcessor::applyOffset($value, $this->setting['value']);
  }

  /**
   * Intelligently applies the given date offset in seconds.
   *
   * Intelligently apply duration values > 1 day, i.e. convert the duration
   * to its biggest possible unit (months, days) and apply it to the date with
   * the given unit. That's necessary as the number of days in a month
   * differs, as well as the number of hours for a day (on DST changes).
   */
  public static function applyOffset($timestamp, $offset) {
    if (abs($offset) >= 86400) {

      // Get the days out of the seconds.
      $days = intval($offset / 86400);
      $sec = $offset % 86400;
      // Get the months out of the number of days.
      $months = intval($days / 30);
      $days = $days % 30;

      // Apply the offset using the DateTime::modify and convert it back to a
      // timestamp.
      $date = date_create("@$timestamp");
      $date->modify("$months months $days days $sec seconds");
      return $date->format('U');
    }
    else {
      return $timestamp + $offset;
    }
  }

}

/**
 * A data processor for applying numerical offsets.
 */
class RulesNumericOffsetProcessor extends RulesDataProcessor {

  /**
   * Overrides RulesDataProcessor::form().
   */
  protected static function form($settings, $var_info) {
    $settings += array('value' => '');
    $form = array(
      '#type' => 'fieldset',
      '#title' => t('Add offset'),
      '#collapsible' => TRUE,
      '#collapsed' => empty($settings['value']),
      '#description' => t('Add an offset to the selected number. E.g. an offset of "1" adds 1 to the number before it is passed on as argument.'),
    );
    $form['value'] = array(
      '#type' => 'number',
      '#title' => t('Offset'),
      '#description' => t('Note that you can also specify negative numbers.'),
      '#default_value' => $settings['value'],
      '#step' => 1,
      '#weight' => 5,
    );
    return $form;
  }

  /**
   * Overrides RulesDataProcessor::process().
   */
  public function process($value, $info, RulesState $state, RulesPlugin $element) {
    $value = isset($this->processor) ? $this->processor->process($value, $info, $state, $element) : $value;
    return $value + $this->setting['value'];
  }

}

/**
 * A custom wrapper class for vocabularies.
 *
 * This class implements RulesDataWrapperSavableInterface, so that
 * Rules can save Vocabularies (which are non-entities).
 */
class RulesTaxonomyVocabularyWrapper extends EntityVocabularyWrapper implements RulesDataWrapperSavableInterface {

}

/**
 * @} End of "addtogroup rules"
 */
