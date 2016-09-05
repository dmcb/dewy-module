<?php

function dewy_report() {
    // Open stream
    $stream = new SplFileObject('php://output', 'w');
    $stream->fwrite('{');

    // Get audit time
    $stream->fwrite('"date":'.json_encode(time()).',');

    // Get title
    $stream->fwrite('"title":'.json_encode(variable_get('site_name')).',');

    // Get paths
    $stream->fwrite('"base_url":'.json_encode($GLOBALS['base_url']).',');

    // Get Drupal version
    $stream->fwrite('"drupal_core":'.json_encode(VERSION).',');

    // Get PHP version
    $stream->fwrite('"php_version":'.json_encode(PHP_VERSION).',');
    
    // Get path accesses
    $stream->fwrite('"traffic":{');
    $ignores = array('admin');
    $result = db_query("SELECT path, hits, lastaccess FROM dewy_counter");
    $stream->fwrite('"recorded_since":'.json_encode(variable_get('dewy_count_date')).',');
    $stream->fwrite('"paths":{');
    $first_row = TRUE;
    while ($path = $result->fetchAssoc()) {
        foreach ($ignores as $ignore) {
            if (substr($path['path'], 0, strlen($ignore)) != $ignore) {
                if (!$first_row) { 
                    $stream->fwrite(',');
                } else {
                    $first_row = FALSE;
                }
                $stream->fwrite(json_encode($path['path']).':'.json_encode(array('hits' => (float)$path['hits'], 'last_access' => (float)$path['lastaccess'])));
            }
        }
    }
    $stream->fwrite('}},');

    // Get the file count and file sizes
    $stream->fwrite('"files":{');
    $types = array('public', 'private');
    $path['public'] = variable_get('file_public_path', conf_path() . '/files');
    $path['private'] = variable_get('file_private_path');

    $first_row = TRUE;
    foreach($types as $type) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }

        $count = 0;
        $size = 0;

        if (is_dir($path[$type])) {
            $iterator = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($path[$type], RecursiveDirectoryIterator::SKIP_DOTS), RecursiveIteratorIterator::SELF_FIRST);
            $filelist = array();
            foreach($iterator as $file) {
                if (!$file->isDir()) {
                    $count++;
                    $size += number_format($file->getSize() / 1048576, 2);
                }
            }
        }
        $stream->fwrite(json_encode($type).':'.json_encode(array('count' => $count, 'size' => $size)));
    }
    $stream->fwrite('},');

    // Get database file size
    $db_size = 0;
    try {
        $database = $GLOBALS['databases']['default']['default']['database'];
        $db_size = db_query("SELECT Round(Sum(data_length + index_length) / 1024 / 1024, 1) as 'mb' FROM information_schema.tables WHERE table_schema = '".$database."';")->fetchField();
    }
    catch (Exception $e) { }
    $stream->fwrite('"db_size":'.json_encode((float)$db_size).',');

    // Get all users, roles and last signons
    $stream->fwrite('"users":{');
    $result = db_query("SELECT users.name AS name, mail, created, access AS last_access, status, GROUP_CONCAT(role.name SEPARATOR ',') AS roles FROM users, role, users_roles WHERE users.uid = users_roles.uid AND users_roles.rid = role.rid GROUP BY name");
    $first_row = TRUE;
    while ($user = $result->fetchAssoc()) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }
        // Obfuscate usernames and email addresses if necessary
        if (!variable_get('dewy_users', 1)) {
            $user['name'] = 'x';
            $user['mail'] = 'x';
        }
        $stream->fwrite(json_encode($user['name']).':'.json_encode(array('mail' => $user['mail'], 'created' => (float)$user['created'], 'last_access' => (float)$user['last_access'], 'status' => $user['status'], 'roles' => explode(',', $user['roles']))));
    }
    $stream->fwrite('},');

    // Get all node content
    $stream->fwrite('"nodes":{');
    $fields = field_info_fields();
    $result = db_query("SELECT nid, type, created, changed FROM node");
    $first_row = TRUE;
    while ($node = $result->fetchAssoc()) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }
        // See what text field data is associate with this node
        $content = array();
        foreach ($fields as $field) {
            if ($field['module'] == 'text') {
                $table = _field_sql_storage_tablename($field);
                $column = _field_sql_storage_columnname($field['field_name'], 'value');
                $field_result = db_select($table, 'field')
                ->fields('field', array($column, 'entity_id'))
                ->condition('entity_type', 'node', '=')
                ->condition('entity_id', $node['nid'], '=')
                ->execute();
                while ($field = $field_result->fetchAssoc()) {
                    // Obfuscate node content if necessary
                    if (!variable_get('dewy_content', 1)) {
                        $field[$column] = preg_replace('/\S+/', 'x',  $field[$column]);
                    }
                    $content[$column] = json_encode($field[$column]);
                }
            }
        }
        $stream->fwrite(json_encode($node['nid']).':'.json_encode(array('created' => (float)$node['created'], 'changed' => (float)$node['changed'], 'type' => $node['type'], 'content' => $content)));
    }
    $stream->fwrite('},');

    // Get all block content
    $stream->fwrite('"blocks":{');
    $result = db_select('block_custom', 'block')
    ->fields('block', array('bid', 'body'))
    ->execute();
    $first_row = TRUE;
    while ($block = $result->fetchAssoc()) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }
        // Obfuscate block content if necessary
        if (!variable_get('dewy_content', 1)) {
            $block['body'] = preg_replace('/\S+/', 'x',  $block['body']);
        }
        $stream->fwrite(json_encode($block['bid']).':'.json_encode($block['body']));
    }
    $stream->fwrite('},');

    // Get all projects and their respective modules/schema updates
    $stream->fwrite('"projects":');
    require_once DRUPAL_ROOT . '/includes/install.inc';
    drupal_load_updates();
    $modules = system_rebuild_module_data();
    $projects = array();
    foreach ($modules as $module => $module_info) {
        $schemas = drupal_get_schema_versions($module_info->name);
        $latest_schema = 0;
        if ($schemas !== FALSE) {
            $latest_schema = max($schemas);
        }
        // Unfortunately I need to show hidden modules, because when you are logged out, more (and essential) modules are set to hidden for
        // a reason I haven't figured out yet
        // if (!$module_info->info['hidden']) {
            if (!isset($projects[$module_info->info['project']])) {
                $projects[$module_info->info['project']] = 
                    array(
                        'modules' => array(
                            $module => array(
                                'schema' => (float)$module_info->schema_version, 
                                'latest_schema' => (float)$latest_schema,
                                'dependencies' => $module_info->info['dependencies'],
                                'hidden' => $module_info->info['hidden']
                            )
                        ), 
                        'version' => $module_info->info['version']
                    );
            } else {
                $projects[$module_info->info['project']]['modules'][$module] = array(
                    'schema' => (float)$module_info->schema_version, 
                    'latest_schema' => (float)$latest_schema,
                    'dependencies' => $module_info->info['dependencies'],
                    'hidden' => $module_info->info['hidden']
                );
            }
        // }
    }
    $stream->fwrite(json_encode($projects));
    $stream->fwrite(',');

    // Get all custom themes
    $stream->fwrite('"themes":{');
    $themes = list_themes(TRUE);
    $first_row = TRUE;
    foreach ($themes as $theme) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }
        $stream->fwrite(json_encode($theme->name).':'.json_encode(array('version' => $theme->info['version'], 'status' => $theme->status)));
    }
    $stream->fwrite('},');

    // Get all variables
    $stream->fwrite('"variables":{');
    $result = db_query("SELECT name FROM variable");
    $first_row = TRUE;
    while ($variable = $result->fetchAssoc()) {
        if (!$first_row) { 
            $stream->fwrite(',');
        } else {
            $first_row = FALSE;
        }
        $stream->fwrite(json_encode($variable['name']).':'.json_encode(variable_get($variable['name'])));
    }
    $stream->fwrite('}');

    // Close stream
    $stream->fwrite('}');
}