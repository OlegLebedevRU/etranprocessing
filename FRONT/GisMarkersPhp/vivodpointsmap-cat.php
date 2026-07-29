<?php
header('Content-Type: text/html; charset=UTF-8');
 
require ("bd.php");

file_put_contents('C:\LOG\file.txt', "\r\naaaaaaaaaaa ", FILE_APPEND); 
if($_SERVER['HTTP_X_REQUESTED_WITH'] == 'XMLHttpRequest') {


$tsql = "SELECT *
         FROM ymapapiv2_markers_cat";
$stmt = sqlsrv_query( $conn, $tsql);
if( $stmt === false )
{
	file_put_contents('C:\LOG\file.txt', "\r\n error", FILE_APPEND); 
     echo "Error in query preparation/execution.\n";
     die( print_r( sqlsrv_errors(), true));
}
else
{
	file_put_contents('C:\LOG\file.txt', "\r\n ok", FILE_APPEND); 

}

/* Retrieve each row as a PHP object and display the results.*/
while( $obj = sqlsrv_fetch_object( $stmt))
{

      //echo $obj->id.", ".$obj->name."\n";

      //echo $obj->id.", ".$obj->name."\n";
	//file_put_contents('C:\LOG\file.txt', "\r\nsqlsrv_fetch_object " + $obj->id, FILE_APPEND); 
	file_put_contents('C:\LOG\file.txt', "\r\nsqlsrv_fetch_object " . $obj->name. " }", FILE_APPEND); 
        //$json =  array(name=>$mar['name'], hinttext=>$mar['hintText'], balloontext=>$mar['balloonText'], styleplacemark=>$stylePlacemark, lat=>$mar['lat'], lon=>$mar['lon']);
	$json[] = $obj;
}

	file_put_contents('C:\LOG\file.txt', "\r\nsqlsrv_fetch_object " . json_encode($json). " }", FILE_APPEND); 
	file_put_contents('C:\LOG\file.txt', "\r\nsqlsrv_fetch_object " . json_encode($json, JSON_FORCE_OBJECT). " }", FILE_APPEND); 

//$json_data = array ('id'=>1,'name'=>"ivan",'country'=>'Russia',"office"=>array("yandex"," management"));
//echo json_encode($json_data);

echo json_encode($json);
//echo "����������� ������� ������� ��� �������: ", json_encode($json, JSON_FORCE_OBJECT), "\n\n";

/*
file_put_contents('C:\LOG\file.txt', "\r\nbbbbbbbbbbbbbb ", FILE_APPEND); 
$type = htmlspecialchars($_GET['cat']);

file_put_contents('C:\LOG\file.txt', "\r\ntype " + $type, FILE_APPEND);  

$result = sqlsrv_query($conn, "SELECT * FROM ymapapiv2_markers_cat");

if($result) 
file_put_contents('C:\LOG\file.txt', "\r\nresult OK", FILE_APPEND);  
else
file_put_contents('C:\LOG\file.txt', "\r\nresult ERROR" , FILE_APPEND);  



$row_count = sqlsrv_num_rows( $result);
if ($row_count === false)
file_put_contents('C:\LOG\file.txt', "\r\nrow_count_error", FILE_APPEND);  
   else if ($row_count >=0)
      file_put_contents('C:\LOG\file.txt', "\r\row_count_ok", FILE_APPEND);  



file_put_contents('C:\LOG\file.txt', "\r\nrow_count "+ $row_count, FILE_APPEND);

if($row_count>0)
{
while ($mar = sqlsrv_fetch_array($result))
{
switch($mar['type'])
{
	case 'bar':
      $stylePlacemark = 'twirl#barIcon';
	  break;
	case 'cafe': 
      $stylePlacemark = 'twirl#cafeIcon';
	  break;	
	default:		 
     $stylePlacemark = 'twirl#restaurauntIcon';
} 



$json =  array(name=>$mar['name'], hinttext=>$mar['hintText'], balloontext=>$mar['balloonText'], styleplacemark=>$stylePlacemark, lat=>$mar['lat'], lon=>$mar['lon']);


//echo "����������� ������� ������� ��� �������: ";
//echo "����������� ������� ������� ��� �������: ", json_encode($json, JSON_FORCE_OBJECT), "\n\n";
//$markers[] = $json;
}
 
}
//$points = array(markers=>$markers);
 
//echo json_encode($points);
 */
}
 
 
?>