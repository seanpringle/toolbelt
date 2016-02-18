<?php

/**
 * Create a Text.
 * @param  string $str
 * @return Text
 */
function text()
{
    return Text::make(join('', func_get_args()));
}

/**
 * True if $obj is a Text.
 * @param  mixed  $obj
 * @return boolean
 */
function is_text($obj)
{
    return is_object($obj) && $obj instanceof Text;
}

/**
 * A string handler.
 *
 * print
 *     text('hello world')
 *         ->trim()->escape();
 * );
 */
class Text implements ArrayAccess, Iterator, Serializable
{
    protected $value = '';
    protected $ipos = 0;
    protected $matches = null;

    const HTML = 1;
    const URL  = 2;

    /**
     * More robust htmlentities()
     * @param string $str
     */
    private static function htmlentities($str)
    {
        $charsets = array(
            'UTF-8',
            'ISO-8859-1',
            'ISO-8859-15',
            'GB2312',
            'BIG5',
            'BIG5-HKSCS',
            'Shift_JIS',
            'EUC-JP',
            'KOI8-R',
            'ISO-8859-5',
            'cp1251',
            'cp1252',
            'MacRoman',
        );

        $test = false;
        foreach ($charsets as $charset)
        {
            if ($test === false) $test = @iconv($charset, 'UTF-8//TRANSLIT', $str);
            if ($test !== false) { $str = $test; break; }
        }

        $flags = ENT_QUOTES;
        if (defined('ENT_SUBSTITUTE')) $flags |= ENT_SUBSTITUTE; // php 5.4
        if (defined('ENT_HTML5'))      $flags |= ENT_HTML5;      // php 5.4

        return htmlentities($str, $flags, 'UTF-8');
    }

    /**
     * [__construct description]
     * @param mixed $val
     */
    public function __construct($val=null)
    {
        $this->value = !is_null($val) ? strval($val): '';
    }

    /**
     * Magic method __toString
     * @return string
     */
    public function __toString()
    {
        return $this->value;
    }

    public static function make($val=null)
    {
        return new static($val);
    }

    public function rewind()
    {
        $this->ipos = 0;
    }

    public function current()
    {
        return substr($this->value, $this->ipos, 1);
    }

    public function key()
    {
        return $this->ipos;
    }

    public function next()
    {
        $this->ipos++;
    }

    public function valid()
    {
        return strlen($this->value) > $this->ipos;
    }

    public function offsetSet($off, $val)
    {
        $this->value[$off] = $val;
    }

    public function offsetUnset($off)
    {
        $this->value[$off] = ' ';
    }

    public function offsetExists($off)
    {
        return strlen($this->value) > $off;
    }

    public function offsetGet($off)
    {
        return $this->value[$off];
    }

    public function serialize()
    {
        return $this->value;
    }

    public function unserialize($str)
    {
        $this->value = $str;
        return $this;
    }

    public function escape()
    {
        $this->value = static::htmlentities($this->value);
        return $this;
    }

    public function trim($mask=" \t\n\r\0\x0B")
    {
        $this->value = trim($this->value, $mask);
        return $this;
    }

    public function ltrim($mask=" \t\n\r\0\x0B")
    {
        $this->value = ltrim($this->value, $mask);
        return $this;
    }

    public function rtrim($mask=" \t\n\r\0\x0B")
    {
        $this->value = rtrim($this->value, $mask);
        return $this;
    }

    public function subtext($start, $length)
    {
        return text(substr($this->value, $start, $length));
    }

    public function match($pattern)
    {
        $this->matches = array();
        return preg_match($pattern, $this->value, $this->matches);
    }

    public function m($n)
    {
        return isset($this->matches[$n]) ? $this->matches[$n]: null;
    }

    public function match_all($pattern)
    {
        $this->matches = array();
        return preg_match_all($pattern, $this->value, $this->matches);
    }

    public function split($pattern)
    {
        if (!$this->value) return array();
        return preg_split($pattern, $this->value);
    }

    public function explode($pattern)
    {
        if (!$this->value) return array();
        return explode($pattern, $this->value);
    }

    public function replace($pattern, $replace='')
    {
        $this->value = preg_replace($pattern, $replace, $this->value);
        return $this;
    }

    public function format($values)
    {
        array_unshift($values, $this->value);
        $this->value = call_user_func_array('sprintf', $values);
        return $this;
    }

    public function url($pairs)
    {
        foreach ($pairs as $name => $value)
            $pairs[$name] = urlencode($name).'='.urlencode($value);
        $this->value .= ($pairs ? '?'.$pairs: '');
        return $this;
    }
}

/**
 * Create a Dict.
 * @param  array  $a
 * @return Dict
 */
function dict($a=array())
{
    if (!is_array($a))
        $a = func_get_args();
    return Dict::make($a);
}

/**
 * True if $obj is a Dict.
 * @param  mixed  $obj
 * @return boolean
 */
function is_dict($obj)
{
    return is_object($obj) && $obj instanceof Dict;
}

/**
 * A light-weight Collection.
 *
 * CakePHP's Collection blows this out fo the water for complex stuff, but
 * sometimes it is handy to have a simple alternative without dependencies.
 * Where possible, to avoid wheel reinvention, this tries to be a thin OO
 * layer over PHP's built-in array functions.
 *
 * print_r(
 *     dict([ 1, 2, 3 ])
 *     ->filter(
 *         function ($key, $val) {
 *             return $val > 1;
 *         })
 *     ->export()
 * );
 */
class Dict implements ArrayAccess, Iterator, Serializable
{
    private $_data = array();
    private $_type = array();

    // Iterator
    private $_ipos = 0;
    private $_ikeys = null;

    /**
     * [__construct description]
     * @param array $row
     */
    public function __construct($row=array())
    {
        $this->import(is_dict($row) ? $row->export(): $row);
    }

    /**
     * Static constructor
     * @param  array  $row
     * @return Dict
     */
    public static function make($row=array())
    {
        return new static($row);
    }

    /**
     * Magic method __get
     * @param  mixed $key scalar
     * @return mixed
     */
    public function __get($key)
    {
        return isset($this->_data[$key]) ? $this->_data[$key]: null;
    }

    /**
     * Magic method __set
     * @param mixed $key scalar
     * @param mixed $val
     * @return void
     */
    public function __set($key, $val)
    {
        if (!isset($this->_type[$key]))
        {
            $this->_type[$key] = gettype($val);
        }
        else
        if (!is_null($val))
        {
            switch ($this->_type[$key])
            {
                case 'boolean':
                    $val = $val ? true: false;
                    break;
                case 'integer':
                    $val = intval($val);
                    break;
                case 'double':
                    $val = floatval($val);
                    break;
                case 'array':
                    $val = is_array($val) ? $val: (array) $val;
                    break;
                case 'object':
                    $val = is_object($val) ? $val: (object) $val;
                    break;
                case 'string':
                    $val = strval($val);
                    break;
            }
        }
        $this->_data[$key] = $val;
    }

    /**
     * Magic method __isset
     * @param  mixed   $key scalar
     * @return boolean
     */
    public function __isset($key)
    {
        return isset($this->_data[$key]);
    }

    /**
     * Magic method __unset
     * @param mixed $key scalar
     */
    public function __unset($key)
    {
        unset($this->_data[$key]);
        unset($this->_type[$key]);
    }

    /**
     * Magic method __toString. Auto-serialize.
     * @return string
     */
    public function __toString()
    {
        return $this->serialize();
    }

    /**
     * Iterator
     * @return void
     */
    public function rewind()
    {
        $this->_ikeys = array_keys($this->_data);
        $this->_ipos = 0;
    }

    /**
     * Iterator
     * @return mixed
     */
    public function current()
    {
        return $this->get($this->_ikeys[$this->_ipos]);
    }

    /**
     * Iterator
     * @return mixed scalar
     */
    public function key()
    {
        return $this->_ikeys[$this->_ipos];
    }

    /**
     * Iterator
     * @return void
     */
    public function next()
    {
        $this->_ipos++;
    }

    /**
     * Iterator
     * @return boolean
     */
    public function valid()
    {
        return isset($this->_ikeys[$this->_ipos]);
    }

    /**
     * ArrayAccess
     * @param  mixed $off scalar
     * @param  mixed $val
     * @return void
     */
    public function offsetSet($off, $val)
    {
        $this->__set($off, $val);
    }

    /**
     * ArrayAccess
     * @param  mixed $off scalar
     * @return void
     */
    public function offsetUnset($off)
    {
        unset($this->_data[$off]);
        unset($this->_type[$off]);
    }

    /**
     * ArrayAccess
     * @param  mixed $off scalar
     * @return void
     */
    public function offsetExists($off)
    {
        return isset($this->_data[$off]);
    }

    /**
     * ArrayAccess
     * @param  mixed $off scalar
     * @return mixed
     */
    public function offsetGet($off)
    {
        return $this->__get($off);
    }

    /**
     * Serializable
     * @return string
     */
    public function serialize()
    {
        return json_encode(array($this->_data, $this->_type));
    }

    /**
     * Serializable
     * @param  string $str
     * @return Dict
     */
    public function unserialize($str)
    {
        list ($this->_data, $this->_type) = json_decode($str, true);
        return $this;
    }

    /**
     * Retrieve value by key.
     * @param  mixed $key scalar
     * @param  mixed $def default value
     * @return mixed
     */
    public function get($key, $def=null)
    {
        return isset($this->_data[$key]) ? $this->$key: $def;
    }

    /**
     * Define a value by key.
     * @param  mixed $key scalar
     * @param  mixed $val
     * @return Dict
     */
    public function set($key, $val=null)
    {
        $this->$key = $val;
        return $this;
    }

    /**
     * Define a value type.
     * @param  mixed $key scalar
     * @param  string $type
     * @return Dict
     */
    public function set_type($key, $type)
    {
        $this->_type[$key] = $type;
        return $this;
    }

    /**
     * Retrieve a value type.
     * @param  mixed $key scalar
     * @return string
     */
    public function get_type($key)
    {
        return isset($this->_type[$key]) ? $this->_type[$key]: null;
    }

    /**
     * Bulk define data.
     * @param  array $row
     * @return Dict
     */
    public function import($row)
    {
        $this->_data = array();
        $this->_type = array();
        foreach ($row as $key => $val)
            $this->$key = $val;
        return $this;
    }

    /**
     * Bulk define data.
     * @param  array $row
     * @return Dict
     */
    public function merge($row)
    {
        foreach ($row as $key => $val)
            $this->$key = $val;
        return $this;
    }

    /**
     * Bulk retrieve data.
     * @return array
     */
    public function export()
    {
        return $this->_data;
    }

    /**
     * Return first item.
     * @return mixed
     */
    public function first()
    {
        return empty($this->_data) ? null: reset($this->_data);
    }

    /**
     * Return first item.
     * @return mixed
     */
    public function last()
    {
        return empty($this->_data) ? null: end($this->_data);
    }

    /**
     * array_column
     * @param  mixed $key
     * @return Dict
     */
    public function column($key)
    {
        return dict(array_column($this->_data, $key));
    }

    /**
     * @return boolean
     */
    public function is_empty()
    {
        return empty($this->_data);
    }

    /**
     * @return int
     */
    public function count()
    {
        return count($this->_data);
    }

    /**
     * @return array
     */
    public function keys()
    {
        return dict(array_keys($this->_data));
    }

    /**
     * @return array
     */
    public function values()
    {
        return dict(array_values($this->_data));
    }

    /**
     * Sort pairs.
     * @param  callable $call
     * @return Dict
     */
    public function sort($call=null)
    {
        if (is_null($call))
            asort($this->_data);
        else
            uasort($this->_data, $call);
        return $this;
    }

    /**
     * Sort pairs.
     * @param  callable $call
     * @return Dict
     */
    public function key_sort($call)
    {
        if (is_null($call))
            ksort($this->_data);
        else
            uksort($this->_data, $call);
        return $this;
    }

    /**
     * @return string
     */
    public function join($c=',')
    {
        return join($c, $this->_data);
    }

    /**
     * Alias for join()
     * @return string
     */
    public function implode($c=',')
    {
        return implode($c, $this->_data);
    }

    /**
     * Clone.
     * @return Dict
     */
    public function copy()
    {
        return clone $this;
    }

    /**
     * Grep
     * @return Dict
     */
    public function grep($pattern, $invert=false)
    {
        foreach ($this->_data as $key => $val)
        {
            $match = preg_match($pattern, $val);
            if (!$match && !$invert) unset($this->$key);
            if ($match && $invert) unset($this->$key);
        }
        return $this;
    }

    /**
     * Grep
     * @return Dict
     */
    public function grepv($pattern)
    {
        return $this->grep($pattern, true);
    }

    /**
     * Apply callback to each pair. Destructive! See copy().
     * @param  callable $call
     * @return Dict
     */
    public function map($call)
    {
        foreach ($this->_data as $key => $val)
        {
            unset($this->_type[$key]);
            $this->$key = $call($key, $val);
        }
        return $this;
    }

    /**
     * Apply callback to each pair, returning a new dict.
     * @param  callable $call
     * @return Dict
     */
    public function each($call)
    {
        $d = dict();
        foreach ($this->_data as $key => $val)
        {
            $d->$key = $call($key, $val);
        }
        return $d;
    }

    /**
     * Apply callback to each pair. Keep matches. Destructive! See copy().
     * @param  callable $call
     * @return Dict
     */
    public function filter($call)
    {
        foreach ($this->_data as $key => $val)
            if (!$call($key, $val)) $this->__unset($key);
        return $this;
    }

    /**
     * Apply callback to each pair. Remove matches. Destructive! See copy().
     * @param  callable $call
     * @return Dict
     */
    public function reject($call)
    {
        foreach ($this->_data as $key => $val)
            if ($call($key, $val)) $this->__unset($key);
        return $this;
    }

    /**
     * Apply callback to each pair, reducing.
     * @param  calable $call
     * @return mixed
     */
    public function reduce($call)
    {
        if (count($this->_data) == 0)
            return null;

        $keys  = array_keys($this->_data);
        $value = $this->_data[array_shift($keys)];

        foreach ($keys as $key)
            $value = $call($value, $this->_data[$key]);

        return $value;
    }

    /**
     * @return int
     */
    public function sum()
    {
        return array_sum($this->_data);
    }

    /**
     * @return mixed
     */
    public function min()
    {
        return min($this->_data);
    }

    /**
     * @return mixed
     */
    public function max()
    {
        return max($this->_data);
    }

    /**
     * True if any value is true.
     * @return boolean
     */
    public function any()
    {
        foreach ($this->_data as $key => $val)
            if ($val) return true;
        return false;
    }

    /**
     * True if any value is true.
     * @param  calable $call
     * @return boolean
     */
    public function some($call)
    {
        foreach ($this->_data as $key => $val)
            if ($call($val)) return true;
        return false;
    }

    /**
     * True if all values are true.
     * @return boolean
     */
    public function all()
    {
        foreach ($this->_data as $key => $val)
            if (!$val) return false;
        return true;
    }

    /**
     * True if all values are true.
     * @param  calable $call
     * @return boolean
     */
    public function every($call)
    {
        foreach ($this->_data as $key => $val)
            if (!$call($val)) return false;
        return true;
    }

    /**
     * True if any value is null.
     * @return boolean
     */
    public function any_null()
    {
        foreach ($this->_data as $key => $val)
            if (is_null($val)) return true;
        return false;
    }

    /**
     * True if all values are null.
     * @return boolean
     */
    public function all_null()
    {
        foreach ($this->_data as $key => $val)
            if (!is_null($val)) return false;
        return true;
    }
}

function query($table='_', $alias=null)
{
    $bt = debug_backtrace();
    $comment = sprintf("%s:%d", basename($bt[0]['file']), $bt[0]['line']);

    $query = new Query($table, $alias ? $alias: substr($table, 0, 1));
    return $query->comment($comment);
}

function is_query($obj)
{
    return $obj instanceof Query;
}

class Query
{
    protected $table;
    protected $alias;
    protected $fields = array();
    protected $where  = array();
    protected $group  = array();
    protected $order  = array();
    protected $limit  = array();
    protected $pairs  = array();
    protected $joins  = array();

    protected $type;

    const SELECT = 1;
    const INSERT = 2;
    const UPDATE = 3;
    const DELETE = 4;
    const REPLACE = 5;

    protected static $db = null;
    protected static $log = array();

    // mysql result resource after each query
    protected $rs = null;
    protected $rs_sql = null;

    protected $error = null;
    protected $error_msg = null;

    // flags
    protected $sql_no_cache = false;

    // calc found
    protected $sql_calc_found = false;
    protected $found_rows = 0;

    protected $comment = null;
    protected $insert_id = null;

    public function __construct($table=null, $alias=null)
    {
        $this->table = $table;
        $this->alias = $alias;
        $this->select();
    }

    public static function connect($con=null)
    {
        $env = dict(is_array($con) ? $con: $_ENV);

        self::$db = pg_connect(
            $env->get('pg_connect', 'host=localhost')
        );
    }

    public function ok()
    {
        if ($this->error)
            throw new Exception($this->error_msg);
    }

    public static function log($sql=null)
    {
        if ($sql)
        {
            while (count(static::$log) > 100)
                array_shift(static::$log);
            static::$log[] = $sql;
        }
        return static::$log;
    }

    public static function error_log($sql=null)
    {
        foreach (self::log($sql) as $line)
            error_log($line);
    }

    public function __toString()
    {
        $table   = $this->sql_table();
        $comment = $this->sql_comment();

        switch ($this->type)
        {
            case self::SELECT:
                $fields = $this->sql_fields();
                $where  = $this->sql_where();
                $group  = $this->sql_group();
                $order  = $this->sql_order();
                $limit  = $this->sql_limit();
                $joins  = $this->sql_joins();
                return "select $comment $fields from $table $joins $where $group $order $limit";

            case self::INSERT:
                $table  = $this->table();
                $fields = join(', ', array_keys($this->pairs));
                $values = join(', ', array_values($this->pairs));
                return "insert $comment into $table ($fields) values ($values)";

            case self::REPLACE:
                $table  = $this->table();
                $fields = join(', ', array_keys($this->pairs));
                $values = join(', ', array_values($this->pairs));
                return "replace $comment into $table ($fields) values ($values)";

            case self::UPDATE:
                $pairs  = $this->sql_pairs();
                $where  = $this->sql_where();
                $order  = $this->sql_order();
                $limit  = $this->sql_limit();
                return "update $comment $table set $pairs $where $order $limit";

            case self::DELETE:
                $where  = $this->sql_where();
                $order  = $this->sql_order();
                $limit  = $this->sql_limit();
                return "delete $comment from $table $where $order $limit";
        }
    }

    public function select()
    {
        $this->type = self::SELECT;
        return $this;
    }
    public function insert()
    {
        $this->type = self::INSERT;
        return $this;
    }
    public function replace()
    {
        $this->type = self::REPLACE;
        return $this;
    }
    public function update()
    {
        $this->type = self::UPDATE;
        return $this;
    }
    public function delete()
    {
        $this->type = self::DELETE;
        return $this;
    }

    public function table($name=null)
    {
        if (!is_null($name))
            $this->table = $name;
        return $this->table;
    }

    public function alias($name=null)
    {
        if (!is_null($name))
            $this->alias = $name;
        return $this->alias;
    }

    public function sql_table()
    {
        $table = is_query($this->table) ? "({$this->table})": $this->table;
        return "$table {$this->alias}";
    }

    public function sql_comment()
    {
        if ($this->comment)
        {
            $comment = str_replace('*/', '', $this->comment);
            return "/* $comment */";
        }
        return '';
    }

    public function sql_fields()
    {
        $sql = array();
        foreach ($this->fields as $alias => $field)
            $sql[] = $field == $alias ? "$field" : "$field as $alias";
        return $sql ? join(', ', $sql): "{$this->alias}.*";
    }

    public function sql_pairs()
    {
        $sql = array();
        foreach ($this->pairs as $field => $value)
            $sql[] = "$field = $value";
        return join(', ', $sql);
    }

    public function sql_where()
    {
        $sql = array();
        foreach ($this->where as $clause)
            $sql[] = is_array($clause) ? '('.join(' or ', $clause).')': "$clause";
        return $sql ? 'where '.join(' and ', $sql): '';
    }

    public function sql_on()
    {
        $sql = array();
        foreach ($this->where as $clause)
            $sql[] = is_array($clause) ? '('.join(' or ', $clause).')': "$clause";
        return $sql ? 'on '.join(' and ', $sql): '';
    }

    public function sql_group()
    {
        $sql = array();
        foreach ($this->group as $field)
            $sql[] = $this->quote_field($field);
        return $sql ? 'group by '.join(', ', $sql): '';
    }

    public function sql_order()
    {
        $sql = array();
        foreach ($this->order as $field => $dir)
            $sql[] = $this->quote_field($field)." $dir";
        return  $sql ? 'order by '.join(', ', $sql): ($this->group ? 'order by null': '');
    }

    public function sql_limit()
    {
        return $this->limit ? 'limit '.$this->limit: '';
    }

    public function sql_joins()
    {
        $sql = array();
        foreach ($this->joins as $join)
        {
            list ($query, $type) = $join;
            $table = $query->table();
            $alias = $query->alias();
            $on    = $query->sql_on();
            $sql[] = "$type join $table as $alias $on";
        }
        return join(' ', $sql);
    }

    private static function quote_number($num)
    {
        return "$num";
    }

    private static function quote_string($str)
    {
        if (preg_match('/^[a-zA-Z0-9@_\-. ]*$/', $str))
            return "'$str'";

        $res = "convert_from(decode('";
        if (strlen($str))
            foreach (str_split($str) as $c)
                $res .= sprintf('%02x', ord($c));
        return $res."', 'hex'), 'UTF-8')";
    }

    // quote and escape a value
    public static function quote_value($val)
    {
        if (is_null($val))
            return 'null';

        if (is_int($val) || is_double($val))
            return self::quote_number($val);

        if (is_scalar($val))
            return self::quote_string($val);

        if (is_array($val))
        {
            $out = array(); foreach ($val as $v) $out[] = self::quote_value( $v);
            return sprintf('(%s)', join(',', $out));
        }

        if (is_query($val))
            return "($val)";

        if (is_object($val))
            return "$val";

        if (is_callable($val))
            return $val();

        return 'null';
    }

    public function quote_field($field)
    {
        if (is_query($field))
            return "($field)";

        if (is_object($field))
            return "$field";

        if (is_scalar($field) && !preg_match('/^[a-zA-Z0-9_]+$/', $field))
            return $field;

        return "\"$field\"";
    }

    public function field($field, $alias=null)
    {
        $this->fields[$alias ? $alias: $field] = $this->quote_field($field);
        return $this;
    }

    public function fields($alias, $fields=array())
    {
        if (!$fields)
        {
            $this->fields["$alias.*"] = "$alias.*";
            return $this;
        }
        foreach ($fields as $row)
        {
            if (is_array($row))
            {
                $this->fields[isset($row[1]) ? $row[1]: $row[0]] = $row[0];
                continue;
            }
            $this->fields[$row] = $row;
        }
        return $this;
    }

    public function set($field, $value=null)
    {
        $this->pairs[self::quote_field($field)] = $this->quote_value($value);
        return $this;
    }

    public function comment($value=null)
    {
        $this->comment = $value;
        return $this;
    }

    public function on($field, $value, $op)
    {
        $this->where($field, $value, $op);
    }

    public function __call($name, $arguments)
    {
        if (preg_match('/^on_(.+)$/', $name, $matches))
        {
            $query = $this->joins[count($this->joins)-1][0];
            call_user_func_array(array( $query, "where_${matches[1]}"), $arguments);
            return $this;
        }
        throw new Exception("unknown method Query::$name", 1);
    }

    public function where($field, $value, $op='eq')
    {
        switch ($op)
        {
            case 'eq': case '=':
                $this->where_eq($field, $value);
                break;
            case 'ne': case '!=': case '<>':
                $this->where_ne($field, $value);
                break;
            case 'lt': case '<':
                $this->where_lt($field, $value);
                break;
            case 'gt': case '>':
                $this->where_lt($field, $value);
                break;
            case 'lte': case '<=':
                $this->where_lt($field, $value);
                break;
            case 'gte': case '>=':
                $this->where_lt($field, $value);
                break;
            case 'in':
                $this->where_in($field, $value);
                break;
            case 'fk':
                $this->where_fk($field, $value);
                break;
            case 'fk_in':
                $this->where_fk_in($field, $value);
                break;
            default:
                throw new Exception("unknown operation Query::where $op", 1);
                break;
        }
        return $this;
    }

    public function where_fk($field, $value)
    {
        $this->where[] = self::quote_field($field).' = '.self::quote_field($value);
        return $this;
    }

    public function where_fk_in($field, $value)
    {
        $this->where[] = self::quote_field($field).' in '.self::quote_field($value);
        return $this;
    }

    public function where_cmp($field, $cmp, $value)
    {
        $this->where[] = self::quote_field($field).' '.$cmp.' '.self::quote_value($value);
        return $this;
    }

    public function where_eq($field,  $value) { return $this->where_cmp($field, '=',  $value); }
    public function where_ne($field,  $value) { return $this->where_cmp($field, '<>', $value); }
    public function where_lt($field,  $value) { return $this->where_cmp($field, '<',  $value); }
    public function where_gt($field,  $value) { return $this->where_cmp($field, '>',  $value); }
    public function where_lte($field, $value) { return $this->where_cmp($field, '<=', $value); }
    public function where_gte($field, $value) { return $this->where_cmp($field, '>=', $value); }

    public function where_like($field, $value) { return $this->where_cmp($field, 'like', $value); }
    public function where_regex($field, $value) { return $this->where_cmp($field, '~', $value); }

    public function where_not($field)
    {
        $this->where[] = 'not '.$this->quote_field($field);
        return $this;
    }

    public function where_null($field, $mode=true)
    {
        $op = $mode ? 'is null': 'is not null';
        $this->where[] = $this->quote_field($field).' '.$op;
        return $this;
    }

    public function where_not_null($field)
    {
        return $this->where_null($field, false);
    }

    public function where_in($field, $value, $mode=true)
    {
        if (is_scalar($value))
            $value = array( $value );

        $op = $mode ? 'in': 'not in';
        $this->where[] = $this->quote_field($field).' '.$op.' '.self::quote_value($value);
        return $this;
    }

    public function where_not_in($field, $value)
    {
        return $this->where_in($field, $value, false);
    }

    public function where_exists($value, $mode=true)
    {
        $op = $mode ? 'exists': 'not exists';
        $this->where[] = $op.' '.self::quote_field($value);
        return $this;
    }

    public function where_not_exists($value)
    {
        return $this->where_exists($value, false);
    }

    function limit($limit, $offset=0)
    {
        $this->limit = $offset ? "$offset $limit": "$limit";
        return $this;
    }

    function order($field, $dir='asc')
    {
        $this->order[$field] = $dir;
        return $this;
    }

    function group($field)
    {
        $this->group[] = $field;
        return $this;
    }

    public function join($table, $alias=null, $type='inner')
    {
        $this->joins[] = array( new Query($table, $alias), $type );
        return $this;
    }

    public function left_join($table, $alias=null)
    {
        return $this->join($table, $alias, 'left');
    }

    private function read($sql=null)
    {
        if (!self::$db)
            self::connect();

        $this->error = null;
        $this->error_msg = null;

        if (!$sql) $sql = "$this";

        self::log($sql);

        $this->rs = pg_query(self::$db, $sql);
        $this->rs_sql = $sql;

        if ($this->rs === false)
        {
            $this->error = 1;
            $this->error_msg = pg_last_error(self::$db);
        }
//        if ($this->sql_calc_found)
//        {
//            $this->found_rows = query()->read('select found_rows()')->fetch_value();
//        }
        return $this;
    }

    private function write($sql=null)
    {
        if (!self::$db)
            self::connect();

        $this->error = null;
        $this->error_msg = null;
        $this->insert_id = null;

        if (!$sql) $sql = "$this";

        self::log($sql);

        $this->rs = pg_query(self::$db, $sql);
        $this->rs_sql = $sql;

        if ($this->rs === false)
        {
            $this->error = 1;
            $this->error_msg = pg_last_error(self::$db);
        }
        else
        {
            $rs = pg_query(self::$db, "select lastval()");
            $row = pg_fetch_array($rs);
            $this->insert_id = $row[0];
        }
        return $this;
    }

    public function execute($sql=null)
    {
        switch ($this->type)
        {
            case self::SELECT:
                return $this->read($sql);

            default:
                return $this->write($sql);
        }

        $this->ok();
    }

    public function fetch_all($index=null)
    {
        if (!$this->rs)
            $this->select()->read()->ok();

        $rows = array();
        while ($this->rs && ($row = pg_fetch_array($this->rs, count($rows), PGSQL_NUM)) && $row)
        {
            $j = count($rows);
            $res = array();
            $pri = array();
            foreach ($row as $i => $value)
            {
                $type  = pg_field_type($this->rs, $i);
                $field = pg_field_name($this->rs, $i);

                $res[$field] = $value;

                if (pg_field_is_null($this->rs, $j, $i))
                {
                    $res[$field] = NULL;
                }
                else
                if (preg_match('/^(int|bigint|smallint)/', $type))
                {
                    $res[$field] = intval($value);
                }
                else
                if (preg_match('/^(float|double)/', $type))
                {
                    $res[$field] = floatval($value);
                }
            }
            $rows[$index ? $res[$index]: $j] = $res;
        }

        foreach ($rows as $i => $row)
            $rows[$i] = dict($row);

        return dict($rows);
    }

    public function fetch_one()
    {
        return $this->limit(1)->fetch_all()->first();
    }

    public function fetch_field($name)
    {
        $values = array();
        foreach ($this->fetch_all() as $row)
            $values[] = $row->count() && $name ? $row->$name: ($row->count() ? array_shift($row->values()): null);
        return dict($values);
    }

    public function fetch_value($name=null)
    {
        $row = $this->fetch_one();
        return $row->count() && $name ? $row->$name: ($row->count() ? array_shift($row->values()): null);
    }

    public function insert_id()
    {
        return $this->insert_id;
    }
}
