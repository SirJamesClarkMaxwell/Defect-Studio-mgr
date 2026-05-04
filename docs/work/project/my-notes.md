1. w `Application.cpp` w `initailizeEventInfrastructure()` jest inicjalizacja `CapabilityService`, `CapabilityRegistry` oraz `Notifier` nie podoba mi sie to. To są osobne systemy które powinny być inicjalizowane gdzie idziej. Najprawdopodobniej w funckji `initializeCoreRuntimeServices`
2. Moduł Loggera jest rozrzucony po folderach, powinien być w jednym miejscu
3. `ApplicationEventController` inicjalizuje logger po przez `ApplicationConfigController` jest to sensowne, o tyle, że aplikacja nie powinna zajmować się konfigurowaniem samej siebie i obsługą zdarzeń do niej przychodzących. Jest to sensowne, ale wtedy aplikacja urasta do bardzo dużego obiektu z wieloma odpowiedzialnościami co mi się jeszcze mniej podoba. Pytanie czy można zmienić ustawienia **Loggera** bez jego reinicjalizacji. Wczejśniej Claude proponował osobną klasę `LoggerControler` co z jednej strony jest sensowne bo jeszcze bardziej uniezależnia kod i go rozprzęga. Przy większej ilości systemów może mieć to sens, ale nie wiem czy aż tak dużo więcej systemów będę miał. 
4. Nie używanie wszędzie naszego `Result` z `StructureError`, brak spójnego systemu zarządzania wyjątkami i komunikowania tego użytkownikowi przez `Notification` i `Logging`
5. W `DemmoLayer.hpp/.cpp` jest wiele klas, służących do pokazania przykładów a powinny być przeniesione do osobnych plików w **DemoLayer**.
6. W `DemmoLayer.cpp` notification jest pobierany z application zamiast korzystać z *dummy-version* żyjącego tylko wewnątrz *DemoLayer*
7. `NotificationCenter` nie jest używany w aplikacjii przykładzie, należy to naprawić.
8. W `NotificationCenter` jest zdefiniowany typ `Listener` który chyba już wcześniej się pojawił i może nastąpić pewna konfuzja czytelnika ze względu na typy. 
9. funkcja `ImGui::InsertNotification({toToastType(severity), 3000, "%s", resolvedMessage.c_str()});` jest wywoływana w bardzo złym miejscu. Powinna być wywoływana jako część renderu a nie trochę losowo w `emmitNotification` która jest lambdą...
10. Wydaje mi się, że to `ImGuiLayer` powinno słuchać na `NotificationEvent` bo to ImGuiLayer powinno renderowac powiadomiania. 
11. `CapabilityRegistry` zdaje się powinno być wewnętrzną częścią `CapabilityService` i cały ruch powinien przechodzić przez `CapabilityService` więc `Application` nie powinno trzymać głównej referencji do `CapabilityRegistry`
12. Brak przykładu jak się rejestruje w `CapabilityService`
13. `Application` z jakiegoś powodu ma **m_BlockingError**. To zdecydownie nie jest miejsce na to
14. W `LoggingPanel` są różne tablice bool, mało czytelne, zmień to na mapy i enumy
15. Wyłącz możliwość klonowania Settings, Logging, Thread, ProgressTracking, TaskMonitor
16. Brak w SettingsPannel wyświetlania skrótów klawiaturowych, i ich możliwości modyfikacji za pomocą klawiatury
17. Nie jestem pewien czy `CommandFactory` i `CommandExecutionObserver` powinny być `std::function` wydaje mi się, że powinny być to osobne klasy (argumenty za i przeciw)
18. `ShowBlockingError` nie pokazuje **blocking window**. Co więcej to powinna być odpowiedzialność ImGuiLayer, aby coś takiego renderować, a nie Application. 
19. Brak ogólnego profilowania aplikacji. Mamy `tracy` ale z niego za bardzo nie korzystamy
20. Są raw-pointer/raw smart_ptr w `CommandRegistry`, `CommandPallet`, `UndoScope`, `CommandContext`
21. Nie ma napisanych globalnie skrótów 
   1. `Ctrl+z`/`Ctrl+y` jako undo-redo
   2. `Ctrl+Shift+p` jako command Pallet
   3. `Ctrl+shift+w` jako application shutdown
   4. `Ctrl+s` jako save 
22. Jest dużo niepotrzebnych **anonymus namespace** 
23. Czemu command-category jest string a nie jakiś enum?
24. Bardzo mi się nie podoba sposób rejestracji komend w DemoLayer
    1.  po pierwsze tworzenie obiektów inplace
    2.  używanie smart_ptr zamiast naszego wrappera na nie
    3.  po drugie przez lambdę
    4.  po trzecie jako akcja jest przekazywana lambda
    5.  `Commandfactory` i `CommandExecutionObserver` są z jakiegoś powodu **std::function** co jest bardzo mylące
    6.  Trzeba przekazywać flags jako `{}` zamiast ustawić coś default
25. Brak jednej klasy spinające system command i undo-redo
26. Funkcja Notify w `CommandRegistry` jest bardzo myląca. Może się mylić z `NotificationSystem` 
27. Implementacja rzeczyw `CommandContext` powinna być w pliku cpp, jeżeli się da. Jeżeli to tempalte to w klasie powinna być tylko deklaracaj a definicja poniżej klasy
28. Dlaczego `command->Execute(context)` zwraca `Result<void>` a nie faktyczy wynik z ewentualnym błędem zaszytym w środku? (to jest pytanie, nie jestem pewien czy taki model byłby lepszy)
29. `CommandOutcome` może powinien mieć jakiś bardziej zmyślny konstruktor? aby nie musieć ustawiać ręcznie w `Execute` jego pól?
30. Wypychanie rzeczy do `UndoStack` wydaje mi się że powinno być robione przez inną klasę a nie `CommandRegistry` np. `CommandService`
31. Klasa `CommandRegistry` ma za dużo odpowiedzialności, przechowuje jednocześnie wszystkie komendy, zarządza nimi oraz zajmuje się ich wykonywaniem oraz powiadamianiem `UndoStack`
32. Jest okej, że korzystamy z `MakeCommandError`. Natomiast nie widzę nigdzie logowania o tym, że rejestracja się gdzieś nie powiodła. Okej Są wpisy do `m_BackendRuntimeLog` ale nie jest to uwzględnione w systemie logowania. Ponieważ to jest w Demo to jeszcze warunkowo to zaakceptuję, ale podczas integracji tego systemu będzie to niedopuszczalne
33. W pliku `KeyBinding.hpp` jest bardzo dużo klas, struktur które zasługują na swoje własne pliki
34. `ContextManager` zdecydowanie powinien być w osobnym pliku
35. Implementacja `CommandContext` powinna być w pliku cpp a to co jest template to poniżej definicji klasy
36. 
37. Funkcje prywatne zawsze powinny być z małej litery





Kolejność wprowadzania poprawek
Faza I
0. (done) Rozbicie DemoLayer do osobnych plików
1. (done) dodanie przykładu z rejestracją Capability
2. (done) Demo-Notifier w DemoLayer zamiast tego z Application
3. (done) Wdrożenie NotificationCenter w DemoLayer oraz usunięcie lambdy `emmitNotification` -> ImGuiLayer powinno słuchać eventów produkowanych podczas proźby o wyświetlenie powiadomienia

Faza II
4. (done) Uniezależnić Core od App (Ryzyko I)
5. (done) Zmiana `Execute` -> `Redo` (Ryzyko II)
6. (done) Zmiana `Publish` -> `Queue` (Ryzyko III)
7. (done) Pozbyć się `Application::Get()` (Ryzyko V)

Faza III
8. Decouple `CommandRegistry` -> Wydzielić `CommandService` z `CommandRegistry`
9.  Dodanie lepszego konstructora dla CommandOutcome
10.  Usunięcie raw-ptr i raw-smart-ptr i zastąpienie z `Memory.hpp`
11.  Rozbicie `KeyBinding` na mniejsze pliki odpowiadające lepiej nazwą i logicznie
12.  Poprawa rejestracji komend w demo (patrz komentarz)
13.  Dodanie ctrl+z, ctrl+y, ctrl+shift+p, ctrl+shift+w, ctrl+s oraz poprawny przechwyt naciśniętych klawyszy (persystencję keybindings w IOLayer) + emit odpowienich eventów
14.  Utworzenie sekcji KeyBinding w settings -> narazie wyświetlanie i wyszukiwanie (po opisie `app.save`/`app.quit`/`app.undo` itd albo przechwytując z klawaitury)
15.  CancelGroup zeruje m_groupDepth zamiast dekrementować -> poprawić 

Faza IV
16.  Naprawa reinicjalizacji Loggera (parz sekcja z komentarza)
17.  Schowanie `CapabilityRegistry` za `CapabilityService`
18.  Usunięcie niepotrzebnych historii Notification
19.  (done) Ujednolicenie `Result<>`
20.  Wyłączyć DS_ASSERT w buildzie Dist (Ryzyko IV)
21.  wyrzucenie z `initializeEventInfrastructure` inicjalizacji `CapabilityService`, `CapabilityRegistry` i `Notifier`
22.  Ujednolicenie plików Logging -> jedno miejsce 
23. (done) Usunąć **DemoLayer** i **DebugLayer** z build Dist (Ryzyko VII)
24. Dodać **RemoveListener** i zmiana nazwy **Listener** w `NotificationCenter`
25. usunąć anonymus namespace
26. usunięcie double `ProcessQueue`

Faza V
27. Rozbicie **Application.cpp** na mniejsze pliki (Ryzyko VI)
28. Przenieść i naprawić m_BlockingError do ImGuiLayer
29. Przenieść render Notification do ImGuiLayer
30. Przenieśc `drawMainPanel/beginFrame/renderFrame` do `ImGuiLayer`
31. zmiana const_cast w EventBus::Publish
32. Naprawić brak logowanie przy błednej błędów rejestracji komend w CommandRegistry
33. zmiana z tablic bool w LoggingPanel na mapy i enumy
34. wyłaczyć możliwość clonowania niektórych paneli (patrz komentarz)
35. szczegółowe profilowanie z tracy
36. modyfikacja skrótów klawiaturowych za pomocą wciśnięcia go na klawiaturze



Komentarze:
1. W `ApplicationConfigController` nie reinicjalizujemy Loggera. Zmieńmy tylko te ustawiania które można zmienić. Reszta może być zablokowana do zmiany
2. To co teraz jest po przez lambdy i implicit object construction jest złe. Jeśli chodzi o lambdy to zdecydowanie bardziej wolę bind funkcji z klasy niż lambda. Tutaj miejscem i ilością lini, nie musimy się przejmować. W realnej aplikacji prawdopodobnie będą to osobne funkcje do rejestracji po kategoriach i one będą w jednym pliku. Miejsce nie jest problemem.
3. Nie chodziło mi o to, że panele to singletony. Bo to nie prawda. W niektórych aplikacjach np. SemiconductorStudio na przykład będzie wiele analiz -> wszystkie panale analiz będą mialy ten sam panel do renderowania ale z różnymi danymi, te powinny dać się duplikować. Natomiast są panele których nie powinno dać sie zduplikować, tak jak te które wymieniłem 