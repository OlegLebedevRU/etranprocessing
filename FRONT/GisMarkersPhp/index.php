<!DOCTYPE HTML PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
    <title>Инструмент для определения координат - API Яндекс.Карт 2.0</title>
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
    <script src="http://api-maps.yandex.ru/2.0/?load=package.full&lang=ru-RU" type="text/javascript"></script>
    <script src="http://yandex.st/jquery/1.6.4/jquery.min.js" type="text/javascript"></script>
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no" />
    <script src="geolocation_service.js" type="text/javascript"></script>

    <style type="text/css">
        html, body, #YMapsID {
            margin: 0;
            padding: 0;
            height: 100%;
        }

        #coord_form {
            position: absolute;
            z-index: 1000;
            background: none repeat scroll 0% 0% rgb(255, 255, 255);
            list-style: none outside none;
            padding: 10px;
            margin: 0px;
            right: 10px;
            top: 50px;
        }

        .input-medium {
            width: 150px;
        }
    </style>



    <script type="text/javascript">

        $(document).ready(function () {

            <?php

               parse_str($_SERVER['QUERY_STRING'], $query);
       
               if($query['admin'])
               { $GLOBALS['admin'] = 'yes';  }
               else { $GLOBALS['admin'] = 'no'; } 
         ?>

        });

        function Confirm()
        {
            var name = $('input[name="name_text"]').val();
            //$("#coord_form").load("addmetki-cat.php", {name: name, hinttext : 'hintText', balloontext : 'balloonText', styleplacemark : 'stylePlacemark', lat : coords[0].toPrecision(6), lon : coords[1].toPrecision(6)});

            $.post(
                "addmetki-cat.php",
                { name: name, hinttext : 'hintText', balloontext : 'balloonText', styleplacemark : 'stylePlacemark', lat : coords[0].toPrecision(6), lon : coords[1].toPrecision(6)},
                function(data) {
                    // $('#stage').html(data);
                }
            );
            var myPlacemarkNew = new ymaps.Placemark(coords);
            //Добавляем метку на карту
            myMap.geoObjects.add(myPlacemarkNew);

            myPlacemarkNew.properties.set({
                balloonContentHeader: name,
                hintContent: 'hintText',
                balloonContent: 'balloonText'
            });
        }

        var myMap, myPlacemark, coords, myCollection, myCoords;
        ymaps.ready(init);

        function init() {

            var service = new GeolocationService(),
    myLocation = service.getLocation({
        // Режим получения наиболее точных данных.
        enableHighAccuracy: true,
        // Максимальное время ожидания ответа (в миллисекундах).
        timeout: 10000,
        // Максимальное время жизни полученных данных (в миллисекундах).
        maximumAge: 1000
    });

            myLocation.then(function (loc) {
                myCoords = [loc.latitude, loc.longitude],
                    myPlacemark = new ymaps.Placemark(myCoords, {}, {
                        preset: "twirl#redIcon", draggable: true
                    });

                myMap = new ymaps.Map('YMapsID', {
                    center: myCoords,
                    zoom: loc.zoom || 9,
                    behaviors: ['default', 'scrollZoom']
                });

                var SearchControl = new ymaps.control.SearchControl({ noPlacemark: true });

                //Добавляем элементы управления на карту
                myMap.controls
                   .add(SearchControl)
                   .add('zoomControl')
                   .add('typeSelector')
                   .add('mapTools');

                myCollection = new ymaps.GeoObjectCollection();
                coords = myCoords;
                myMap.geoObjects.add(myPlacemark);


                SearchControl.events.add("resultselect", function (e) {
                    coords = SearchControl.getResultsArray()[0].geometry.getCoordinates();
                    savecoordinats();
                });

                <?php if($GLOBALS['admin'] == 'yes') { ?>

                myPlacemark.events.add("dragend", function (e) {
                    coords = this.geometry.getCoordinates();
                    savecoordinats();
                }, myPlacemark);

                myMap.events.add('click', function (e) {
                    coords = e.get('coordPosition');
                    savecoordinats();
                });


                //Ослеживаем событие изменения области просмотра карты - масштаб и центр карты
                myMap.events.add('boundschange', function (event) {
                    if (event.get('newZoom') != event.get('oldZoom')) {
                        savecoordinats();
                    }
                    if (event.get('newCenter') != event.get('oldCenter')) {
                        savecoordinats();
                    }
                });
                <?php } ?>


                //Запрос данных и вывод маркеров на карту
                $.getJSON("vivodpointsmap-cat.php",
                        function (json) {
                            $.each(json, function (i, item) {
                                myCollection.add(new ymaps.Placemark([item.lat, item.lon],
                                        {
                                            // Свойства
                                            hintContent: item.hintText,
                                            balloonContentHeader: item.name,
                                            balloonContentBody: item.balloonText
                                        }));
                            });
                            myMap.geoObjects.add(myCollection);
                        });

            });

        }
        

        //Функция для передачи полученных значений в форму
        function savecoordinats() {

            var new_coords = [coords[0].toFixed(4), coords[1].toFixed(4)];
            myPlacemark.getOverlay().getData().geometry.setCoordinates(new_coords);
            <?php if($GLOBALS['admin'] == 'yes') { ?>
            document.getElementById("latlongmet").value = new_coords;
            document.getElementById("mapzoom").value = myMap.getZoom();
            var center = myMap.getCenter();
            var new_center = [center[0].toFixed(4), center[1].toFixed(4)];

            document.getElementById("latlongcenter").value = new_center;
            <?php } ?>

        }
        

    </script>
</head>
<body>

    <div id="YMapsID"></div>

    <?php if($GLOBALS['admin'] == 'yes') { ?>
    <div id="coord_form">
        <p>

            <label>Координаты метки: </label>
            <br />
            <input id="latlongmet" class="input-medium" name="icon_text" /><br />
            <label>Масштаб: </label>
            <br />
            <input id="mapzoom" class="input-medium" name="icon_text" />

        </p>
        <p>
            <label>Центр карты: </label>
            <br />
            <input id="latlongcenter" class="input-medium" name="icon_text" /></p>

        <p>
            <label>Название:</label>
            <br />
            <input type="text" class="input-medium" name="name_text" />
        </p>
        <p>
            <button name="mysubmitbutton" onclick="Confirm()" id="mysubmitbutton" type="submit" class="customButton">
                Добавить метку
            </button>
        </p>
    </div>
    <?php } ?>

</body>
</html>
