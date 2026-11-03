# Návod na použitie: detektor hluku a generátor signálu

Sústavu tvoria dve zariadenia na báze ESP32-C3:

- **Detektor** (decibelmeter) meria hladinu zvuku. Keď prekročí nastavený prah, pošle generátoru bezdrôtovú správu.
- **Generátor** po prijatí správy spustí na výstupe signál s preladovanou frekvenciou.

Zariadenia spolu komunikujú priamo cez ESP-NOW, takže nepotrebujú Wi-Fi router ani internet.

Návod platí pre firmvér `dd-detector` `e2b9bbf` a `dd-generator` `898fab0`.

## Dôležité vedieť vopred

**Webová stránka každého zariadenia je dostupná len prvých 5 minút po zapnutí.** Potom sa jeho Wi-Fi sieť vypne a zariadenie pracuje ďalej bez nej. Nastavenia zostanú uložené a detekcia aj spúšťanie generátora fungujú ďalej. Stránku znova otvoríte iba reštartom, teda vypnutím a zapnutím napájania.

Pri nastavovaní preto zapnite obe zariadenia naraz, aby ste mali otvorené obe stránky súčasne.

## Zapojenie

| Zariadenie | Pin | Funkcia |
|---|---|---|
| Detektor | GPIO21 | Príjem dát zo senzora (pripojte na TX senzora), 115 200 baud, 8N1 |
| Detektor | GPIO20 | Vysielanie do senzora (pripojte na RX senzora) |
| Generátor | GPIO3 | Výstup signálu |

Piny ESP32-C3 pracujú s logikou 3,3 V. Výstup generátora je slabý obdĺžnikový signál (PWM, strieda 50 %), takže reproduktor k nemu pripojte cez zosilňovač.

Generátor umiestnite tak, aby ho mikrofón detektora nepočul. Inak môže jeho vlastný signál prekročiť prah a spúšťať ho stále dokola.

## Pripojenie na stránku zariadenia

| | Detektor | Generátor |
|---|---|---|
| Wi-Fi sieť | `Detector` | `Generator` |
| Heslo | `123456789` | `123456789` |
| Adresa stránky | `http://192.168.1.150` | `http://192.168.4.1` |

1. Do 5 minút od zapnutia sa pripojte telefónom alebo počítačom na Wi-Fi sieť zariadenia.
2. Stránka by sa mala otvoriť sama ako prihlasovacie okno siete. Ak sa neotvorí, zadajte adresu z tabuľky do prehliadača.
3. Ak telefón hlási, že sieť nemá internet, zvoľte, že chcete zostať pripojení.

V hornej časti stránky vidíte, koľko času zostáva do jej zatvorenia. Telefón môže byť naraz len na jednej Wi-Fi sieti, takže pri prechode medzi zariadeniami treba sieť prepnúť.

## Prvé nastavenie

Stačí vykonať raz, nastavenia sa ukladajú a prejavia sa hneď po uložení.

### 1. Generátor

Otvorte stránku generátora a v časti **Settings** vyplňte všetky polia:

| Pole | Význam | Podmienka |
|---|---|---|
| Minimum frequency (Hz) | Najnižšia frekvencia | aspoň 1 |
| Maximum frequency (Hz) | Najvyššia frekvencia | aspoň ako minimum |
| Step size (Hz) | O koľko sa frekvencia zmení v každom kroku | 0 a viac |
| Time per step (ms) | Ako dlho trvá jeden krok | 0 a viac |
| Delay before start (ms) | Čakanie medzi prijatím správy a štartom | 0 a viac |
| Run time (s) | Ako dlho signál znie | aspoň 1 |
| Detector MAC address | Od ktorého detektora prijímať správy | prázdne = od ľubovoľného |

Všetky čísla musia byť celé. Frekvencia sa počas behu preladuje od minima k maximu a späť. **Kým generátor nemá uložené nastavenia, správy od detektora ignoruje.**

Stlačte **Save settings** a opíšte si adresu z časti **This device**. Tú zadáte do detektora.

### 2. Detektor

Otvorte stránku detektora a v časti **Settings** vyplňte:

- **Threshold (dB):** prah od 0 do 200. Bez prahu detektor nič nevyhodnocuje ani neposiela.
- **Generator MAC address:** adresa opísaná z generátora vo formáte `aa:bb:cc:dd:ee:ff`.

Stlačte **Save settings**.

### 3. Spárovanie opačným smerom (voliteľné)

Ak chcete, aby generátor poslúchal iba váš detektor, opíšte adresu z riadku *This device's MAC* na stránke detektora do poľa **Detector MAC address** na generátore. Odporúča sa to, ak je v dosahu iný takýto detektor.

### 4. Skúška spojenia a spustenia

1. Na detektore stlačte **Connect to generator** a počkajte niekoľko sekúnd.
2. Skontrolujte stav v časti **Generator link**. So súčasným firmvérom je správny výsledok **No data** (pozrite [Známe obmedzenia](#známe-obmedzenia)). Znamená, že generátor rádiovo odpovedal.
3. Na detektore dočasne nastavte prah pod aktuálnu hladinu a uložte. Na generátore by mal rásť údaj **Start triggers** a výstup by sa mal spustiť.
4. Vráťte prah na požadovanú hodnotu a uložte.

## Bežná prevádzka

Stačí zapnúť napájanie, webovú stránku nepotrebujete. Po 5 minútach sa Wi-Fi vypne a zariadenia si ďalej vymieňajú len správy o prekročení prahu.

Detektor vyhodnocuje hladinu približne 2- až 3-krát za sekundu a pri každom meraní nad prahom pošle správu. Ak správa príde počas behu generátora, po jeho skončení sa spustí ešte jeden beh. Pri trvalom hluku nad prahom teda generátor beží nepretržite.

## Čo zobrazujú stránky

### Detektor

- **Live level:** aktuálna hladina, vodorovný ukazovateľ so značkou prahu a graf za posledné 2 minúty. Graf sa začína kresliť až po otvorení stránky.
- **Stav hladiny:** `Below threshold`, `Over threshold`, `No threshold set` (nie je nastavený prah), `Waiting for sensor` alebo `Sensor lost` (zo senzora neprichádzajú platné dáta).
- **Generator link:** stav spojenia a prenos dát. Hodnoty *Sent* a *Received* sú priemery za 10 sekúnd. Dáta sa posielajú len pri spojení a prekročení prahu, takže 0 B/s je na tichom spojení normálnych.
- **Delivered:** percento správ, ktoré generátor potvrdil.
- **Statistics:** minimum, priemer a maximum od reštartu, počet meraní nad prahom a ďalšie údaje. *Over threshold* počíta jednotlivé merania, nie samostatné udalosti, takže 10 sekúnd hluku sa započíta viackrát. Údaj *all time* sa pri reštarte nemaže, ostatné áno. Občasné *Bad sensor frames* sú normálne.

| Stav spojenia na detektore | Význam |
|---|---|
| No generator set | Nie je zadaná adresa generátora, správy sa vysielajú všetkým v dosahu |
| Not connected | Spojenie ste ešte neskúšali |
| Connecting… / Waiting for data… | Skúška prebieha |
| No data | Generátor rádiovo odpovedal, ale neposlal dáta |
| Connected | Od generátora prišli dáta |
| Not reachable | Generátor neodpovedal |
| Link lost | Posledné správy neboli doručené |

### Generátor

- **Start triggers:** počet prijatých správ o prekročení prahu. Pod ním je stav `Generating now`, `Waiting for trigger` alebo `Save settings to enable`.
- **ESP-NOW link:** prijaté a odoslané dáta, sila signálu a doručenie.
- **Upozornenie „Ignored … frames from …“:** generátor dostáva správy zo zariadenia s inou adresou, než má nastavenú. Ak je to váš detektor, opravte adresu v nastaveniach.

## Riešenie problémov

| Problém | Čo skontrolovať |
|---|---|
| Wi-Fi sieť zariadenia nevidno | Od zapnutia prešlo viac ako 5 minút. Vypnite a zapnite napájanie. |
| Stránka hlási, že sa portál zatvoril alebo zariadenie nie je dostupné | To isté: 5 minút uplynulo. Ak to tak nie je, skontrolujte, či je telefón stále na správnej sieti. |
| Stránka sa neotvorí sama | Zadajte adresu do prehliadača vrátane `http://`. Vypnite mobilné dáta. |
| `Waiting for sensor` alebo `Sensor lost` | Zapojenie a napájanie senzora, prehodené RX a TX. |
| `Not reachable` | Zapnutý generátor, správna adresa (z časti *This device* na generátore), vzdialenosť. |
| Generátor sa nespúšťa, *Start triggers* nerastie | Nastavený prah na detektore, hladina nad prahom, adresy na oboch stranách. Pozrite aj upozornenie *Ignored*. |
| *Start triggers* rastie, ale výstup je bez signálu | Uložené nastavenia generátora (stav nesmie byť `Save settings to enable`), zapojenie GPIO3 a zosilňovača, frekvencia v počuteľnom rozsahu. |
| Generátor beží stále dokola | Trvalý hluk nad prahom alebo detektor počuje samotný generátor. Zvýšte prah alebo zariadenia oddeľte. |
| Spojenie vypadáva | Obe zariadenia používajú pevne Wi-Fi kanál 1. Rušiť ich môže iná sieť na tomto kanáli. Skúste ich priblížiť alebo premiestniť. |

## Známe obmedzenia

- **Tlačidlo „Connect to detector“ na generátore zatiaľ s týmto firmvérom detektora nedokončí spojenie.** Po približne 10 sekundách ukáže `No response` a živú hladinu nezobrazí. Spúšťanie generátora tým nie je ovplyvnené, funguje aj bez spojenia.
- **Detektor preto pri skúške spojenia končí v stave `No data`.** Ak je zároveň aktívne pripájanie na generátore, môže na chvíľu ukázať `Connected` a v poli *Data from generator* technické údaje. Znamená to, že rádiové spojenie funguje, nie že sa prenáša hladina.
- Heslo Wi-Fi je pre obe zariadenia pevne dané vo firmvéri a nedá sa zmeniť cez stránku.
- Správy medzi zariadeniami nie sú šifrované.
