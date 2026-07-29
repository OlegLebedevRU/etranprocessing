<?php

//ini_set('mssql.charset', 'cp1251');

$serverName = "revery\sqlexpress"; 
$connectionInfo = array( "Database"=>"db2", "UID"=>"phpuser", "PWD"=>"phppwd", "CharacterSet" => "UTF-8");
$conn = sqlsrv_connect( $serverName, $connectionInfo);

if(! $conn ) {
file_put_contents('C:\LOG\file.txt', "\r\nDB connection ERROR", FILE_APPEND);
  echo "<br>Db connection error<br>";
  exit();
    }
else
{
file_put_contents('C:\LOG\file.txt', "\r\nDB connection OK", FILE_APPEND);

    //$conn.exec("SET NAMES = UTF-8");
    //ini_set('mssql.charset', 'UTF-8');
    //ini_set('mssql.charset', 'cp1252');


}
 
?>
