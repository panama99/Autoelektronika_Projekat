# Autoelektronika Projekat

## Uvod

Mjerenje nivoa goriva u automobilu

## Ideja i zadaci: 
 Napisati softver za simulaciju sistema za mjerenje nivoa goriva u automobilu. 
 Napraviti minimum 4 taska, jedan task za prijem podataka od PC-ja, jedan task za slanje podataka PC-iju, drugi za mjerenje nivoa goriva,
 i treci za prikaz na displeju.
 Sihronizaciju izmedju tajmera i taskova, kao i izmedju taskova ako je potrebno, realizovati pomocu semafora (ili task notifikacija) ili mutexa, 
 zavisno od potrebe. Podatke izmedju taskova slati preko redova (queue).

Trenutni nivo goriva simulirati pomoću UniCom, simulatora “serijske” komunikacije. Računati da se informacije o trenutnom nivou goriva dobijaju preko UniCom 
softvera svake sekunde na kanalu 0. Nivo goriva se dobija kao vrijednost otpornosti, maksimalne vrijednosti 10K. 
Komunikaciju sa PC-ijem ostvariti isto preko simulatora serijske veze, ali na kanalu 1.
Za simulaciju displeja koristiti Seg7Mux, a za simulaciju logičih ulaza i izlaza koristiti LED_bar.

## Periferije

Periferije koje je potrebno koristiti su LED_bar, 7seg displej i UniCom softver za simulaciju serijske komunikacije.
Prilikom pokretanja LED_bars_plus.exe navesti BB kao argument da bi se dobio led bar sa 2 izlazna stupca plave boje.
Prilikom pokretanja Seg7_Mux.exe navesti kao argument broj 10, kako bi se dobio 7-seg displej sa 10 cifara.
Što se tiče serijske komunikacije, potrebno je otvoriti i kanal 0 i kanal 1. Kanal 0 se automatski otvara pokretanjem UniCom.exe, a kanal
1 otvoriti dodavanjem broja jedan kao argument: AdvUniCom.exe 1

## Kratak pregled taskova

Glavni .c fajl ovog projekta je main_application.c

### void SerialSend_Task0(void* pvParameters)
S obzirom da vrijednost trenutne otpornosti treba dobiti svaki sekund sa kanala 0 serijske komunikacije, od strane FreeRTOS-a je to omogućeno tako što se u ovom tasku svakih 1s šalje karakter 'T' preko kanala 0. Kada se pokrene AdvUniCom.exe potrebno je označiti opciju AUTO, odnosno svaki put kad stigne karakter 'a', da se pošalje naredba oblika VxxxxR. 
Vrijednost koja se pošalje je zapravo vrijednost trenutne otpornosti (npr. 50). Povremeno je potrebno ručno u AdvUniCom softveru mijenjati ovu vrijednost 
kako bi se simulirala promjena nivoa goriva u automobilu.

### void SerialReceive_Task0(void* pvParameters)
Ovaj task ima za zadatak da obradi podatke koji stižu sa kanala 0 serijske komunikacije.
To je podatak o trenutnoj vrijednosti otpornosti. Karakteri se smještaju u niz iz kog se izvlači vrednost (50) i smješta se u red, 
kako bi ostali taskovi taj podatak imali na raspolaganju za dalje računanje. Ovaj task "čeka" semafor koji će da pošalje interrupt rutina svaki put
kada neki karakter stigne na kanal 0.

### void SerialReceive_Task1(void* pvParameters)
Ovaj task ima za zadatak da obradi podatke koji stižu sa kanala 1 serijske komunikacije.
Naredbe koje stižu su formata \00naredba\0d. Primeri su: \00MINFUEL10\0d, \00MAXFUEL9000\0d, \00PP8\0d. 
Ovaj task će iz ovih  naredbi izvući vrijednost za MINFUEL (u ovom primjeru 10), vrednost za MAXFUEL (u ovom primjeru 9000), 
vrednost za potrosnju u litrima na 100km (u ovom primeru 8). Te vrijednosti smjestiće u globalne promijenljive koje se koriste u drugim taskovima. 
Task takođe kao i prethodni čeka odgovarajući semafor da bi se odblokirao i izvršio. Taj semafor daje interrupt rutina svaki put kada pristigne karakter na kanal 1.

### void SerialSend_Task1(void* pvParameters)
Ovaj task ima za zadatak da šalje trenutnu vrijednost goriva u procentima na kanal 1 serijske komunikacije svakih 1s.
Task šalje samo pod uslovom da su parametri MINFUEL i MAXFUEL uneti, odnosno poslati prethodno sa kanala 1. 


### void PercentageFuelLevel (void* pvParameters)
Ovaj task ima za zadatak da preračuna trenutni nivo goriva u procentima i izračuna koliko još km moze automobil da se kreće sa trenutnom količinom goriva. 
Ukoliko je nivo goriva u procentima manji od 10% pali diode. Računanje je omogućeno tek kada su svi parametri MINFUEL, MAXFUEL i PP (potrošnja u litrima) unijeti,
odnosno pristigli sa kanala 1 serijske komunikacije, u suprotnom nema smisla računati.
Task preuzima iz reda vrijednost otpornosti i računa na osnovu formula potrebne informacije.

### void AverageFuelLevel(void* pvParameters)
Ovaj task računa prosijek poslednjih 5 vrijednosti pristiglih otpornosti koje preuzima iz reda i ukoliko je potrebno ispisuje prosek na terminal. 

### void Display7Segment_LEDbar(void* pvParameters)
Task koji ispisuje na 7seg displej informacije, brzina osvezavanja 100ms.
Ispisuje nivo goriva u procentima i otpornost.

### void main_demo(void)
U ovoj funkciji se vrši inicijalizacija svih periferija koje se koriste, kreiraju se taskovi, semafori i red, definiše se interrupt za serijsku komunikaciju i 
poziva vTaskStartScheduler() funkcija koja aktivira planer za raspoređivanje taskova.

## Predlog simulacije cijelog sistema
Prvo otvoriti sve periferije na način opisan gore. Pokrenuti program. U prozoru AdvUniCom softvera kanala 0, upisati da kada stigne karakter 'T', 
kanal 0 šalje naredbu formata npr V50R.
Čekirati Auto i ok1. Na taj način je trenutna otpornost koja se šalje npr. 50 oma i šalje se svaki sekund ka FreeRTOS-u. 
Promijenom te vrijednosti, simulira se pristizanje različitih vrijednosti otpornosti. U kanalu 1 serijske komunikacije poslati naredbe za 
MINFUEL, MAXFUEL i PP(potrošnju). Na primjer: \00MINFUEL10\0d , zatim \00MAXFUEL9000\0d, zatim \00PP8\0d Početak poruke označava hex 0x00, 
kraj poruke CR (carriage return) koji je u hex formatu 0x0d. Na taj način su unijeti parametri, na osnovu kojih je moguće i ima smisla dalje računati.
MINFUEL tada ima vrednost 10, MAXFUEL je 9000 i potrošnja je 8l na 100km. Tada u prozoru kanala 1 se pojavljuje nivo goriva u procentima. 
Uneti na primjer vrijednost otpornosti 70. Tada će zasvetleti u LED_bar-u odgovarajuće diode, jer je za 
tu vrijednost otpornosti nivo goriva u procentima manji od 10%. Kada se vrati stara vrednost npr. 7800 oma, tada se ta dioda isključuje. Takodje ispisivace se u terminalu i 
koliko kilometara jos automobil moze da nastavi sa voznjom a 7seg displej ce sve vrijeme ispisivati procenat goriva i otpornost koja je ocitana.
  
