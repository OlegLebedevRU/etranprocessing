<?php

header('Content-Type: text/html; charset=UTF-8');

include("bd.php");

require_once "html_filter_class.php";



	$tags_set = array(
		
		'h1'		=> array('id', 'class'),
		'h2'		=> array('id', 'class'),
		'h3'		=> array('id', 'class'),
		'h4'		=> array('id', 'class'),
		'h5'		=> array('id', 'class'),
		'h6'		=> array('id', 'class'),
		
		'p'			=> array('id', 'class'),
		'span'		=> array('id', 'class'),
		'a'			=> array('id', 'class', 'href'),
		'img'		=> array('id', 'class', 'src', 'alt', FALSE),
		'br'		=> array(FALSE),
		'hr'		=> array(FALSE),
		
		'strong'		=> array('id', 'class'),	
		'div'		=> array('id', 'class', 'style'),		
		
		
		'ul'		=> array('id', 'class'),
		'ol'		=> array('id', 'class'),
		'li'		=> array('id', 'class'),
		
		'table'		=> array('id', 'class'),
		'tr'		=> array('id', 'class'),
		'td'		=> array('id', 'class'),
		'th'		=> array('id', 'class'),
		'thead'		=> array('id', 'class'),
		'tbody'		=> array('id', 'class'),
		'tfoot'		=> array('id', 'class')	
		
	);
	
	
	$html_filter = new html_filter();
	$html_filter->set_tags($tags_set);

$name = htmlspecialchars($_POST['name']);
        $hintText = htmlspecialchars($_POST['hinttext']);
        $balloonText = $html_filter->filter($_POST['balloontext']);
        $stylePlacemark = $_POST['styleplacemark'];

        switch($stylePlacemark)
        {
            case 'twirl#barIcon':
                $type = 'bar';
                break;
            case 'twirl#cafeIcon':
                $type = 'cafe';
	  break;	
	default:		 
     $type = 'restauraunt';
} 


$lat = $_POST['lat'];
$lon = $_POST['lon'];

$house=iconv('utf-8', 'windows-1251', $name);


file_put_contents('C:\LOG\file.txt', "\r\nadd metki name " .$house, FILE_APPEND);
file_put_contents('C:\LOG\file.txt', "\r\nadd metki name " .$_POST['name'], FILE_APPEND);

$sql = "INSERT INTO ymapapiv2_markers_cat (
		[name]
           ,[hintText]
           ,[balloonText]
           ,[type]
           ,[lat]
           ,[lon]
) VALUES ('$name', '$hintText', '$balloonText', '$type', '$lat', '$lon');";

$result = sqlsrv_query($conn, $sql) or die("Error request: " . "SQL ERROR");

echo $stylePlacemark;

?>