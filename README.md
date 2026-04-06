Program citygmlAI generuje obiekty w formacie binarnym .e3d. wykorzystując przy tym
dane node track zapisane w scenerii wyeksportowanej z programu
Rainsted https://rainsted.com/pl/Strona_g%C5%82%C3%B3wna. Wygenerowane tym programem
pliki można wczytać do Symulatora Maszyna EU07 https://store.steampowered.com/app/1033030/MaSzyna/.
Pliki z danymi GML można pobrać z Geoportalu https://www.geoportal.gov.pl/.

Program wyamga plików pugiconfig.hpp, pugixml.cpp i pugixml.hpp. Do pobrania ba stronie
https://github.com/zeux/pugixml/tree/master/src

Testowałem na danych LOD1 oraz na LOD2. Niestety na zachodniej połowie Polski dostępne są
wyłącznie dane LOD1, a w nich obiekty mają płaskie dachy.

Program wymaga też pliku .ini, gdzie ustawiamy podstawowe parametry, nazwy katalogów i plików.
