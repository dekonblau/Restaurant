<?php
/**
 * @file
 * Database interface code for MySQL database servers.
 */

/**
 * @addtogroup database
 * @{
 */

/**
 * MySQL database connection driver.
 */
class DatabaseConnection_mysql extends DatabaseConnection {
  /**
   * Flag to indicate if the cleanup function in __destruct() should run.
   *
   * @var boolean
   */
  protected $needsCleanup = FALSE;

  /**
   * Flag set in utf8mb4IsSupported().
   *
   * @var boolean
   */
  protected $utf8mb4Supported;

  /**
   * Constructs a MySQL-based DatabaseConnection.
   */
  public function __construct(array $connection_options = array()) {
    // Default to transaction support, unless explicitly set FALSE.
    $this->transactionSupport = !isset($connection_options['transactions']) || ($connection_options['transactions'] !== FALSE);

    // MySQL never supports transactional DDL.
    $this->transactionalDDLSupport = FALSE;

    $this->connectionOptions = $connection_options;

    $charset = 'utf8';
    // Check if the charset is overridden to utf8mb4 in settings.php.
    if ($this->utf8mb4IsActive()) {
      $charset = 'utf8mb4';
    }

    // The DSN should use either a socket or a host/port.
    if (isset($connection_options['unix_socket'])) {
      $dsn = 'mysql:unix_socket=' . $connection_options['unix_socket'];
    }
    else {
      // Default to TCP connection on port 3306.
      $dsn = 'mysql:host=' . $connection_options['host'] . ';port=' . (empty($connection_options['port']) ? 3306 : $connection_options['port']);
    }
    // Character set is added to dsn to ensure PDO uses the proper character
    // set when escaping. This has security implications. See
    // https://www.drupal.org/node/1201452 for further discussion.
    $dsn .= ';charset=' . $charset;
    if (!empty($connection_options['database'])) {
      $dsn .= ';dbname=' . $connection_options['database'];
    }
    // Allow PDO options to be overridden.
    $connection_options += array(
      'pdo' => array(),
    );
    $connection_options['pdo'] += array(
      // Make sure MySQL returns all matched rows on update queries including
      // rows that actually didn't have to be updated because the values didn't
      // change. This matches common behaviour among other database systems.
      PDO::MYSQL_ATTR_FOUND_ROWS => TRUE,
      // Avoid mess around with cursors and unbuffered queries by default.
      PDO::MYSQL_ATTR_USE_BUFFERED_QUERY => TRUE,
      // MySQL prepared statements skip the query cache, because it's dumb.
      PDO::ATTR_EMULATE_PREPARES => TRUE,
      // Convert numeric values to strings when fetching. In PHP 8.1,
      // PDO::ATTR_EMULATE_PREPARES now behaves the same way as non emulated
      // prepares and returns integers. See https://externals.io/message/113294
      // for further discussion.
      PDO::ATTR_STRINGIFY_FETCHES => TRUE,
      // Limit SQL to a single statement like mysqli.
      PDO::MYSQL_ATTR_MULTI_STATEMENTS => FALSE,
    );

    parent::__construct($dsn, $connection_options['username'], $connection_options['password'], $connection_options['pdo']);

    // Force MySQL to use the UTF-8 character set. Also set the collation, if a
    // certain one has been set; otherwise, MySQL defaults to 'utf8_general_ci'
    // for UTF-8.
    if (!empty($connection_options['collation'])) {
      $this->pdo->exec('SET NAMES ' . $charset . ' COLLATE ' . $connection_options['collation']);
    }
    else {
      $this->pdo->exec('SET NAMES ' . $charset);
    }

    // Set MySQL init_commands if not already defined.  Default Backdrop's MySQL
    // behavior to conform more closely to SQL standards.  This allows Backdrop
    // to run almost seamlessly on many different kinds of database systems.
    // Also, as MySQL 5.5 changed the meaning of TRADITIONAL we need to spell
    // out the modes one by one.
    $connection_options += array(
      'init_commands' => array(),
    );
    $connection_options['init_commands'] += array(
      'sql_mode' => "SET sql_mode = 'REAL_AS_FLOAT,PIPES_AS_CONCAT,ANSI_QUOTES,IGNORE_SPACE,STRICT_TRANS_TABLES,STRICT_ALL_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO'",
    );
    // Execute initial commands.
    foreach ($connection_options['init_commands'] as $sql) {
      $this->pdo->exec($sql);
    }
  }

  /**
   * {@inheritdoc}
   */
  protected function setPrefix($prefix) {
    if (is_array($prefix)) {
      $this->prefixes = $prefix + array('default' => '');
    }
    else {
      $this->prefixes = array('default' => $prefix);
    }

    // This differs from the parent class implementation in that all tables
    // are escaped with back-ticks. First, do table-specific replacements.
    $this->prefixSearch = array();
    $this->prefixReplace = array();
    foreach ($this->prefixes as $key => $val) {
      if ($key != 'default') {
        $this->prefixSearch[] = '{' . $key . '}';
        // Add backticks around the entire table name to avoid MySQL keywords,
        // but also add backticks within the prefix value, in the event
        // different database names are used as a prefix.
        $this->prefixReplace[] = '`' . str_replace('.', '`.`', $val) . $key . '`';
      }
    }
    // Then replace remaining tables with the default prefix.
    $this->prefixSearch[] = '{';
    $this->prefixReplace[] = '`' . $this->prefixes['default'];
    $this->prefixSearch[] = '}';
    $this->prefixReplace[] = '`';
  }

  /**
   * {@inheritdoc}
   */
  public function escapeField($field) {
    $field = parent::escapeField($field);
    return $this->quoteIdentifier($field);
  }

  /**
   * {@inheritdoc}
   */
  public function escapeAlias($field) {
    $field = parent::escapeAlias($field);
    return $this->quoteIdentifier($field);
  }

  /**
   * Quotes an identifier with backticks for MySQL 8 compatibility.
   *
   * Not all identifiers need quotes, only keywords, but we add them on all
   * fields and table names for consistency and to ease compatibility in the
   * future.
   *
   * @param string $identifier
   *   The field to check.
   *
   * @return string
   *   The identifier, quoted with backticks.
   */
  private function quoteIdentifier($identifier) {
    // Some identifiers are empty, such as when doing expressions.
    if (strlen($identifier) === 0) {
      return $identifier;
    }
    // Identifiers that have both a table and field should quote both.
    elseif (strpos($identifier, '.') !== FALSE) {
      list($table, $identifier) = explode('.', $identifier, 2);
      return "`$table`" . '.' . "`$identifier`";
    }
    // Otherwise quote just the field name.
    return "`$identifier`";
  }

  /**
   * Destroy a MySQL database connection object.
   */
  public function __destruct() {
    if ($this->needsCleanup) {
      $this->nextIdDelete();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function queryRange($query, $from, $count, array $args = array(), array $options = array()) {
    return $this->query($query . ' LIMIT ' . (int) $from . ', ' . (int) $count, $args, $options);
  }

  /**
   * {@inheritdoc}
   */
  public function queryTemporary($query, array $args = array(), array $options = array()) {
    $tablename = $this->generateTemporaryTableName();
    $this->query('CREATE TEMPORARY TABLE {' . $tablename . '} Engine=MEMORY ' . $query, $args, $options);
    return $tablename;
  }

  /**
   * {@inheritdoc}
   */
  public function driver() {
    return 'mysql';
  }

  /**
   * {@inheritdoc}
   */
  public function version() {
    $version = parent::version();

    // Some MariaDB version strings prepend a fake MySQL version to the front,
    // which is always "5.5.5-". This was done to so that old MySQL clients
    // could connect to newer MariaDB servers.
    // For example, 5.5.5-10.5.11-MariaDB:
    // - 5.5.5 is a fake MySQL version.
    // - 10.5.11 would be the actual MariaDB version.
    // See https://github.com/MariaDB/server/blob/f6633bf058802ad7da8196d01fd19d75c53f7274/include/mysql_com.h#L42
    // Remove any leading MySQL 5.5.5- prefixes:
    $regex = '/^(?:5\.5\.5-)?(\d+\.\d+\.\d+.*-mariadb.*)/i';
    preg_match($regex, $version, $matches);
    if (!empty($matches[1])) {
      $version = $matches[1];
    }

    return $version;
  }

  /**
   * {@inheritdoc}
   */
  public function databaseType() {
    return 'mysql';
  }

  /**
   * {@inheritdoc}
   */
  public function createDatabase($database) {
    try {
      // Create the database and set it as active.
      $this->pdo->exec("CREATE DATABASE IF NOT EXISTS $database");
      $this->pdo->exec("USE $database");
    }
    catch (\Exception $e) {
      throw new DatabaseNotFoundException($e->getMessage());
    }
  }

  /**
   * {@inheritdoc}
   */
  public function mapConditionOperator($operator) {
    // We don't want to override any of the defaults.
    return NULL;
  }

  /**
   * {@inheritdoc}
   */
  public function nextId($existing_id = 0) {
    $new_id = $this->query('INSERT INTO {sequences} () VALUES ()', array(), array('return' => Database::RETURN_INSERT_ID));
    // This should only happen after an import or similar event.
    if ($existing_id >= $new_id) {
      // If we INSERT a value manually into the sequences table, on the next
      // INSERT, MySQL will generate a larger value. However, there is no way
      // of knowing whether this value already exists in the table. MySQL
      // provides an INSERT IGNORE which would work, but that can mask problems
      // other than duplicate keys. Instead, we use INSERT ... ON DUPLICATE KEY
      // UPDATE in such a way that the UPDATE does not do anything. This way,
      // duplicate keys do not generate errors but everything else does.
      $this->query('INSERT INTO {sequences} (value) VALUES (:value) ON DUPLICATE KEY UPDATE value = value', array(':value' => $existing_id));
      $new_id = $this->query('INSERT INTO {sequences} () VALUES ()', array(), array('return' => Database::RETURN_INSERT_ID));
    }
    $this->needsCleanup = TRUE;
    return $new_id;
  }

  /**
   * Clean up the sequences table when closing a database connection.
   *
   * @see DatabaseConnection_mysql::__destruct()
   */
  public function nextIdDelete() {
    // While we want to clean up the table to keep it up from occupying too
    // much storage and memory, we must keep the highest value in the table
    // because InnoDB uses an in-memory auto-increment counter as long as the
    // server runs. When the server is stopped and restarted, InnoDB
    // re-initializes the counter for each table for the first INSERT to the
    // table based solely on values from the table so deleting all values would
    // be a problem in this case. Also, TRUNCATE resets the auto increment
    // counter.
    try {
      $max_id = $this->query('SELECT MAX(value) FROM {sequences}')->fetchField();
      // We know we are using MySQL here, no need for the slower db_delete().
      $this->query('DELETE FROM {sequences} WHERE value < :value', array(':value' => $max_id));
    }
    // During testing, this function is called from shutdown with the
    // simpletest prefix stored in $this->connection, and those tables are gone
    // by the time shutdown is called so we need to ignore the database
    // errors. There is no problem with completely ignoring errors here: if
    // these queries fail, the sequence will work just fine, just use a bit
    // more database storage and memory.
    catch (PDOException $e) {
    }
  }

  /**
   * {@inheritdoc}
   *
   * Overridden to work around issues to MySQL not supporting transactional DDL.
   */
  protected function popCommittableTransactions() {
    // Commit all the committable layers.
    foreach (array_reverse($this->transactionLayers) as $name => $active) {
      // Stop once we found an active transaction.
      if ($active) {
        break;
      }

      // If there are no more layers left then we should commit.
      unset($this->transactionLayers[$name]);
      if (empty($this->transactionLayers)) {
        if (!$this->doCommit()) {
          throw new DatabaseTransactionCommitFailedException();
        }
      }
      else {
        // Attempt to release this savepoint in the standard way.
        try {
          $this->query('RELEASE SAVEPOINT ' . $name);
        }
        catch (PDOException $e) {
          // However, in MySQL (InnoDB), savepoints are automatically committed
          // when tables are altered or created (DDL transactions are not
          // supported). This can cause exceptions due to trying to release
          // savepoints which no longer exist.
          //
          // To avoid exceptions when no actual error has occurred, we silently
          // succeed for MySQL error code 1305 ("SAVEPOINT does not exist").
          if ($e->errorInfo[1] == '1305') {
            // If one SAVEPOINT was released automatically, then all were.
            // Therefore, clean the transaction stack.
            $this->transactionLayers = array();
            // We also have to explain to PDO that the transaction stack has
            // been cleaned-up.
            $this->doCommit();
          }
          else {
            throw $e;
          }
        }
      }
    }
  }

  /**
   * Do the actual commit, including a workaround for PHP 8 behaviour changes.
   *
   * @return bool
   *   Success or otherwise of the commit.
   */
  protected function doCommit() {
    if ($this->pdo->inTransaction()) {
      return $this->pdo->commit();
    }
    else {
      // In PHP 8.0 a PDOException is thrown when a commit is attempted with no
      // transaction active. In previous PHP versions this failed silently.
      return TRUE;
    }
  }

  /**
   * {@inheritdoc}
   */
  public function rollback($savepoint_name = 'backdrop_transaction') {
    // MySQL will automatically commit transactions when tables are altered or
    // created (DDL transactions are not supported). Prevent triggering an
    // exception to ensure that the error that has caused the rollback is
    // properly reported.
    if (!$this->pdo->inTransaction()) {
      // On PHP 7 $this->connection->inTransaction() will return TRUE and
      // $this->connection->rollback() does not throw an exception; the
      // following code is unreachable.
      // If \DatabaseConnection::rollback() would throw an
      // exception then continue to throw an exception.
      if (!$this->inTransaction()) {
        throw new DatabaseTransactionNoActiveException();
      }
      // A previous rollback to an earlier savepoint may mean that the savepoint
      // in question has already been accidentally committed.
      if (!isset($this->transactionLayers[$savepoint_name])) {
        throw new DatabaseTransactionNoActiveException();
      }

      trigger_error('Rollback attempted when there is no active transaction. This can cause data integrity issues.', E_USER_WARNING);
      return NULL;
    }
    return parent::rollback($savepoint_name);
  }

  /**
   * {@inheritdoc}
   */
  public function utf8mb4IsActive() {
    return isset($this->connectionOptions['charset']) && $this->connectionOptions['charset'] === 'utf8mb4';
  }

  /**
   * {@inheritdoc}
   */
  public function utf8mb4IsSupported() {
    // Caching if this method is called multiple times in the same request.
    if (isset($this->utf8mb4Supported)) {
      return $this->utf8mb4Supported;
    }

    // Ensure that the MySQL server supports large prefixes and utf8mb4.
    try {
      $this->query("DROP TABLE IF EXISTS {backdrop_utf8mb4_test}");
      $this->query("CREATE TABLE {backdrop_utf8mb4_test} (id VARCHAR(255), PRIMARY KEY(id(255))) CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci ROW_FORMAT=DYNAMIC ENGINE=INNODB");
    }
    catch (Exception $e) {
      $this->utf8mb4Supported = FALSE;
      return FALSE;
    }
    $this->query("DROP TABLE IF EXISTS {backdrop_utf8mb4_test}");
    $this->utf8mb4Supported = TRUE;
    return TRUE;
  }
}


/**
 * @} End of "addtogroup database".
 */
