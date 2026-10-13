# Budowanie trudnych komórek gizmo zamiast grup przestrzennych

Status: propozycja (2026-10-13), konsultacja codex (read-only). Do decyzji użytkownika: kolejność wycinków.

Użytkownik (2026-10-13): generator z grupy przestrzennej / Wyckoffa „vesta już tak robi i nie jest to wygodne”.
Cel: wygodne budowanie (np. chalkopiryt CuFeS2) dla kogoś, kto nie zna grup punktowych ani pozycji Wyckoffa,
lubi UI i myśli prosto: przyciąganie w trybie modalnym `g`/`r` + oś, transformacje względem innych atomów.

## Model: Umieść → Powiel → Przypisz pierwiastki → Dopasuj

| Akcja | Klawisze | Co widać |
|---|---|---|
| Ruch/obrót/skala | `G/R/S`, potem `X/Y/Z`; `Shift+oś` = płaszczyzna; uchwyty gizmo te same reguły | w orientacji Sieć osie opisane a/b/c |
| Wpis wartości | `G X 1/8 Enter` = ⅛a w orientacji Sieć; przełącznik jednostek na Å; obrót w stopniach | przesunięcie ułamkowe **i** w Å; błędne wyrażenie blokuje Enter |
| Przyciąganie | magnes: **Siatka / Cele**; `Ctrl` włącza, `Ctrl+Shift` dokładniej | siatka 1/2, 1/4, 1/8, 1/3, 1/6, 1/12; kąty 15/30/45/60/90/120° |
| Przyciąganie do geometrii | najechanie: atomy, środki wiązań, narożniki, podziały krawędzi, środki ścian; `Tab` przełącza nakładające się cele | podświetlony cel i punkt zaczepienia |
| Odniesienia | w `G/R`: `V` = wskaż A → B (oś A→B); `R V` = obrót wokół wiązania; `P` = inny punkt obrotu (np. atom) | narysowana oś A→B |
| Odległość od atomu | w `G`: `D`, wskaż atom, wpisz odległość | „Odległość od Cu12: 2.35 Å” |
| Powiel i przekształć | `Shift+D` → kopie w miejscu + `G` (`R` zmienia na obrót); `Shift+R` powtarza ostatnią operację | jedno zatwierdzenie = jedna operacja undo |
| Szereg / lustro | „Powtórz…” (liczba + wskazany wektor), „Odbij…” (punkt, trzy atomy lub ściana komórki; przenieś/kopiuj) | podgląd kopii, liczba, ostrzeżenie o kolizjach |

Nagłówek: `Ruch · a (Sieć) · +1/8a = 0.68 Å · Siatka 1/8 · Zaczep Cu12`, pod nim podpowiedzi klawiszy.

Dalsze akcje nazwane:
- **Tryb Komórka** (przełącznik Atomy / Komórka): uchwyty końców a/b/c, długości, kąty, c/a; zawsze widoczne
  „zachowaj pozycje ułamkowe / kartezjańskie”. **Powtórz komórkę 1×1×2** powiela atomy i powiększa komórkę.
- **Dodaj podsieć**: wizualne wzory (narożniki, środki ścian, środek), pierwiastek, przesunięcie; pod spodem
  istniejące `ExpandBasisOverLattice`.
- **Wypełnij** (motyw na siatce) i **Zawiń do komórki** – osobno.
- **Przypisz wzór**: pomaluj mały motyw Cu/Fe i powtórz w przestrzeni, z podglądem kolorów (nie według indeksu).
- **Wyczyść pozycje**: podgląd przesunięć do pobliskich ułamków w tolerancji; parametry jak u siarki zostają.
- **Powiązane pozycje** (później, domyślnie wyłączone): ruch wszystkich równoważnych atomów (spglib w tle,
  bez słów „symetria”).

## Decyzje domyślne (Claude 2026-10-13, użytkownik nieobecny – do zmiany)

1. Oba style: nazwane akcje (podsieć, powtórz komórkę) dla prostego przepisu **i** swobodne G/R z celami.
2. Orientacja Sieć = domyślnie ułamki wektorów (`U` przełącza na Å w trakcie operacji).
3. `Shift+D` + Esc usuwa oczekujące kopie (nakładające się atomy to niewidoczny błąd); stare `Ctrl+D`
   bez zmian do czasu 112b.
4. Zmiana komórki domyślnie zachowuje pozycje ułamkowe.
5. „Powiązane pozycje” – odłożone (wycinek 5), wtedy pytanie: symetria czy kopie motywu.

Kolejność: 112a (ułamki w G/R, przyciąganie w ułamkach sieci, nagłówek z jednostkami) → 112b (`Shift+D`
z podglądem) → 112c (komórka: c/a, powtórz 1×1×2, zawiń) → 112d (dodaj podsieć) → wycinek 2 (cele).

## Co już jest

- Modalne G/R/S z orientacjami Global/Local/Sieć/Defekt, płaszczyzny, punkt obrotu (mediana / kursor 3D /
  własne środki), przyciąganie w Å i stopniach, wpis liczby: `Renderer/Scene/ModalTransform.{hpp,cpp}`.
  Wpis przyjmuje tylko cyfry, kropkę i znak (`ModalTransform.cpp:310`); przyciąganie w Sieci działa na
  znormalizowanych wektorach, więc w Å (`ModalTransform.cpp:151`).
- Kursor 3D na atomie / środku zaznaczenia: `RendererPanelContextMenu.cpp:231`.
- `Ctrl+D` powiela i wchodzi w ruch, ale z przesunięciem 0.5 Å i Esc zostawia kopie
  (`ViewportInteraction.cpp:64`, `RendererAtomEditCommands.cpp:224`).
- Dodaj/usuń/zmień pierwiastek jako komendy z undo; rozwinięcie podsieci i superkomórka w Domain.

## Pułapki

- Komórki skośne: przyciąganie ułamkowe przez pełną macierz komórki i jej odwrotność.
- Cele przez granicę komórki: atom **plus przesunięcie obrazu**; pokazać, który obraz wybrano.
- Podgląd niezawinięty, zawijanie jawne; kolizje raportować, nie usuwać po cichu.
- Ułamki: współrzędne w domenie są `float` – trzecie części dokładnie wymagają zapamiętania przepisu.
- Undo: powiel+przekształć, szereg, wzór, zmiana komórki = po jednej operacji.

## Wycinki (szacunki codex w dniach pracy człowieka)

| Wycinek | Zakres | Rozmiar |
|---|---|---:|
| 1 | Wpis ułamków, przyciąganie w ułamkach sieci, jednostki; `Shift+D` z podglądem; Dodaj podsieć; Powtórz komórkę; edytor komórki z c/a; Zawiń | 12–18 |
| 2 | Cele (atomy, środki, komórka), osie/punkty obrotu/odległości ze wskazanych atomów | 7–12 |
| 3 | Szereg, lustro, malowanie wzoru pierwiastków | 6–10 |
| 4 | Pełne gizmo komórki, czyszczenie pozycji | 5–8 |
| 5 | Powiązane pozycje (spglib) | 8–15 |

Test akceptacyjny wycinka 1 – chalkopiryt z blendy cynkowej (baza AFLOW ABC2_tI16_122_a_b_d):
1. Kationy jako podsieć FCC, kopia jako S przesunięta o (1/4,1/4,1/4).
2. Powtórz komórkę 1×1×2 → 16 pozycji.
3. Cu na (0,0,0), (0,1/2,1/4), (1/2,1/2,1/2), (1/2,0,3/4); pozostałe 4 kationy → Fe.
4. Siarka: δ = u − 1/4; (1/4,1/4,1/8) i jej kopia +(1/2,1/2,1/2): +δa; (3/4,3/4,1/8): −δa;
   (1/4,3/4,3/8): +δb; (3/4,1/4,3/8): −δb.
5. Ustaw c/a, zachowując pozycje ułamkowe.
