function setMapCookie(map) {
    document.cookie = 'googlemaplng=' + map.getCenter().lng();
    document.cookie = 'googlemaplat=' + map.getCenter().lat();
    var zoom = map.getZoom();
    document.cookie = 'googlemapzoom=' + zoom;
    document.layoutform.scaling_factor.value =+ zoom;
}

function readCookie(name) {
    var nameEQ = name + "=";
    var ca = document.cookie.split(';');
    for(var i=0;i < ca.length;i++) {
        var c = ca[i];
        while (c.charAt(0)==' ') c = c.substring(1,c.length);
        if (c.indexOf(nameEQ) == 0) {
            c=c.substring(nameEQ.length,c.length);
            while (c.substring(c.length-1, c.length) == ' ')
                c = c.substring(0,c.length-1);
            return c;
        }
    }
    return null;
}

function googlemap() {
    var googlemaplat = readCookie('googlemaplat');
    var googlemaplng = readCookie('googlemaplng');
    var scale = readCookie('googlemapzoom') || gstatusmap.scale || 100;
    if (googlemaplat != null  && googlemaplng != null) {
        var nbLatlng = new google.maps.LatLng(googlemaplat, googlemaplng);
    } else {
        var nbLatlng = new google.maps.LatLng(gstatusmap.lat, gstatusmap.lng);
    }
    var mapOptions = {
        zoom: parseInt(scale),
        center: nbLatlng,
        mapTypeId: google.maps.MapTypeId.ROADMAP,
        mapTypeControl: true,
        scaleControl: true,
        overviewMapControl: true,
        overviewMapControlOptions: {
             opened: true
        }
    };
    var map = new google.maps.Map(document.getElementById('map-canvas'),
        mapOptions);
    document.layoutform.scaling_factor.value = scale;
    google.maps.event.addListener(map, "click", function () {
        setMapCookie(map);
    });
    google.maps.event.addListener(map, "move", function () {
        setMapCookie(map);
    });
    google.maps.event.addListener(map, "zoom", function () {
        setMapCookie(map);
    });
    // insert markers
    setMarkers(map);
}

function setMarkers(map){
    for (var x in gstatusmap.markers) {
    var vmark = gstatusmap.markers[x];
        if (vmark) {
            var hostname = vmark[0];
            var notes = vmark[1];
            var address = vmark[2];
            var lat = vmark[3];
            var long = vmark[4];
            var state = vmark[5];
            var point = new google.maps.LatLng(lat, long);
            if (state == "Up") {
                var imageicon = gstatusmap.images_url + "googleup.png";
            }else if(state == "Down") {
                var imageicon = gstatusmap.images_url + "googledown.png";
            }else if(state == "Unreachable") {
                var imageicon = gstatusmap.images_url + "googleurb.png";
            }else if(state == "Pending") {
                var imageicon = gstatusmap.images_url + "googlepend.png";
            }else if(state == "Acknowledged") {
                var imageicon = gstatusmap.images_url + "googleack.png";
            }
            var marker = new google.maps.Marker({
                position: point,
                map: map,
                icon: imageicon,
                title: hostname
            });
        }
    }
}

google.maps.event.addDomListener(window, 'load', googlemap);
