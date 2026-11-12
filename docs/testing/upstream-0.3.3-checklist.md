# Перевірка всіх 40 пунктів v0.3.3 у форку

Перевірено код після інтеграції `cd9af54` проти upstream `bf08ba1`.
17 нововведень і 23 пункти виправлень зі списку користувача мають реалізацію
у поточних джерелах. Таблиця підтверджує **наявність коду та його підключення**,
а не успішний ігровий прогін: GTA, її SCM, графіка та фізика тут не запускались.

Два конфлікти виправлено під час повторної перевірки:

1. `FollowHost` раніше повертався для будь-якого сюжетного автомобіля, блокуючи
   новий upstream-механізм власного транспорту й автоматичної посадки, зокрема
   у літак/гелікоптер. Прибрано старий паралельний автопарк хоста; кожен напарник
   тепер використовує механізм upstream, коли зайняті всі місця.
2. У `ApplyMirror` згасання застарілого керування одразу перезаписувалось
   початковими значеннями; після фізичного кроку вони теж відновлювались.
   Обидві ділянки тепер використовують свіжість мережевого стану.

## Нововведення

| № | Пункт | Реалізація | Відмінність / примітка |
| --- | --- | --- | --- |
| N01 | Вибір місткості, спільний старт місії, запрошення до підробітків і гонок | Є: [ConnectMenu.cpp:494](../../src/client/pages/connect-menu/ConnectMenu.cpp#L494), [Missions.cpp:2463](../../src/client/features/story/missions/Missions.cpp#L2463), [Race.cpp:172](../../src/client/features/story/race/Race.cpp#L172) | F9: 2–8, сервер: максимум 8; більярд і змагання залишаються парними. |
| N02 | Шляхи напарників у 130 катсценах | Є: [ScenePartner.cpp:2926](../../src/client/features/story/cutscene-partner/ScenePartner.cpp#L2926), [Missions.cpp:3123](../../src/client/features/story/missions/Missions.cpp#L3123) | Авторські шляхи для перших трьох напарників; додаткові чотири мають окремі місця з перевіркою перешкод. За відсутності безпечного місця персонаж прихований у кадрі. |
| N03 | U: анімації, попередній перегляд, парні анімації, пакети й власні IFP | Є: [Emotes.cpp:750](../../src/client/features/anims/Emotes.cpp#L750), [AnimPreview.cpp:135](../../src/client/features/anims/AnimPreview.cpp#L135), [AnimsMenu.cpp:117](../../src/client/pages/anims-menu/AnimsMenu.cpp#L117) | Каталог і 12 пакетів перенесені без змін з upstream; збережене колесо J. |
| N04 | Турель і RC-іграшки у місіях Зеро, магніт | Є: [RcToys.cpp:408](../../src/client/features/story/rc-toys/RcToys.cpp#L408) | Перенесено з upstream. |
| N05 | Другий стрілець у The Da Nang Thang / Reuniting the Families | Є: [Missions.cpp:1062](../../src/client/features/story/missions/Missions.cpp#L1062) | Позиції стрільців рознесені для всіх семи напарників. |
| N06 | Dam and Blast, Amphibious Assault, Cop Wheels, Catalyst, ефекти й тривога | Є: [MissionRules.cpp:76](../../src/client/features/story/mission-rules/MissionRules.cpp#L76), [Missions.cpp:1105](../../src/client/features/story/missions/Missions.cpp#L1105) | Перенесено з upstream. |
| N07 | Власний транспорт, літак, гелікоптер і джетпак | Є: [Players.cpp:2371](../../src/client/entities/player/Players.cpp#L2371) | Виправлено конфлікт: старий автопарк більше не блокує видачу та автоматичну посадку. |
| N08 | Очікування напарників біля цілі до 45 секунд | Є: [Missions.cpp:1535](../../src/client/features/story/missions/Missions.cpp#L1535) | Перенесено з upstream. |
| N09 | Спільне життя, провал місії та відродження хоста | Є: [Missions.cpp:1132](../../src/client/features/story/missions/Missions.cpp#L1132), [Missions.cpp:1171](../../src/client/features/story/missions/Missions.cpp#L1171) | Перенесено з upstream. |
| N10 | Спільні гонки: власні чекпойнти, позиція й показ пасажирам | Є: [Race.cpp:172](../../src/client/features/story/race/Race.cpp#L172) | Перенесені сюжетні й побічні гонки upstream. Фонові вуличні гонки, раніше вимкнені у форку, залишаються вимкненими. |
| N11 | Більярд зі ставкою | Є: [Pool.cpp:60](../../src/client/features/story/pool/Pool.cpp#L60) | Перенесено з upstream. |
| N12 | Одночасні школи, спільні гаражі, експорт і прогрес підробітків | Є: [Vehicles.cpp:1028](../../src/client/entities/vehicle/Vehicles.cpp#L1028), [Garages.cpp:237](../../src/client/features/garages/Garages.cpp#L237), [Exports.cpp:60](../../src/client/features/story/exports/Exports.cpp#L60), [Progress.cpp:417](../../src/client/features/progress/Progress.cpp#L417) | Гаражі спільні за правилами upstream; власність на будинки з форка збережена. |
| N13 | Вільне прицілювання водія, стрільба на мотоциклі та ракетниця пасажира | Є: [Players.cpp:313](../../src/client/entities/player/Players.cpp#L313), [Shots.cpp:48](../../src/client/features/shots/Shots.cpp#L48) | Перенесено з upstream. |
| N14 | Радіо водія з однаковою позицією відтворення | Є: [Radio.cpp:426](../../src/client/features/radio/Radio.cpp#L426) | Перенесено з upstream. |
| N15 | F2: гроші, зброя й телепорти; F9: клавіші; F7; найближчі двері на G | Є: [PlayersMenu.cpp:142](../../src/client/pages/players-menu/PlayersMenu.cpp#L142), [ConnectMenu.cpp:37](../../src/client/pages/connect-menu/ConnectMenu.cpp#L37), [Core.cpp:122](../../src/client/app/core/Core.cpp#L122), [Players.cpp:1965](../../src/client/entities/player/Players.cpp#L1965) | Перенесено з upstream. |
| N16 | Окремий прогрес для кожної гри хоста, відновлення зброї зі збереження | Є: [Progress.cpp:1050](../../src/client/features/progress/Progress.cpp#L1050), [Server.cpp:566](../../src/server/app/server/Server.cpp#L566), [Economy.cpp:124](../../src/client/entities/economy/Economy.cpp#L124) | Збережено читання старих профілів форка; серверні баланси теж розділені за грою. |
| N17 | Місійний розшук біля хоста, власний розшук далеко та вилучення зброї | Є: [Missions.cpp:696](../../src/client/features/story/missions/Missions.cpp#L696), [Missions.cpp:525](../../src/client/features/story/missions/Missions.cpp#L525) | Перенесено з upstream. |

## Виправлення

| № | Пункт | Реалізація | Відмінність / примітка |
| --- | --- | --- | --- |
| F01 | Вильоти: Small Town Bank, Angel Pine, одна машина, завантаження, спортзал | Є: [Vehicles.cpp:49](../../src/client/entities/vehicle/Vehicles.cpp#L49), [Patches.cpp:789](../../src/client/app/patches/Patches.cpp#L789), [Missions.cpp:1605](../../src/client/features/story/missions/Missions.cpp#L1605), [Missions.cpp:1564](../../src/client/features/story/missions/Missions.cpp#L1564), [Patches.cpp:834](../../src/client/app/patches/Patches.cpp#L834) | Перенесено з upstream. |
| F02 | Виліт при посадці за бортовий кулемет | Є: [Missions.cpp:1006](../../src/client/features/story/missions/Missions.cpp#L1006) | Перенесено з upstream. |
| F03 | Спеціальні персонажі з модами на ліміти | Є: [Peds.cpp:397](../../src/client/entities/ped/Peds.cpp#L397), [Progress.cpp:1103](../../src/client/features/progress/Progress.cpp#L1103) | Перенесено з upstream. |
| F04 | Очищення нескінченного мінігана після Air Raid | Є: [Missions.cpp:1824](../../src/client/features/story/missions/Missions.cpp#L1824), [RcToys.cpp:162](../../src/client/features/story/rc-toys/RcToys.cpp#L162) | Перенесено з upstream. |
| F05 | Без зірки за машину іншого гравця | Є: [Vehicles.cpp:432](../../src/client/entities/vehicle/Vehicles.cpp#L432) | Перенесено з upstream. |
| F06 | Stowaway і Black Project | Є: [Missions.cpp:599](../../src/client/features/story/missions/Missions.cpp#L599), [Missions.cpp:2010](../../src/client/features/story/missions/Missions.cpp#L2010) | Перенесено з upstream. |
| F07 | Головні убори у катсценах | Є: [Patches.cpp:387](../../src/client/app/patches/Patches.cpp#L387) | Перенесено з upstream. |
| F08 | Відновлення машин і пішоходів після місій | Є: [Missions.cpp:2020](../../src/client/features/story/missions/Missions.cpp#L2020), [Population.cpp:49](../../src/client/features/population/Population.cpp#L49) | Перенесено з upstream. |
| F09 | Солдати Robbing Uncle Sam, SWAT/FBI/армія/гелікоптери та ящики | Є: [Peds.cpp:757](../../src/client/entities/ped/Peds.cpp#L757), [Objects.cpp:72](../../src/client/features/mission-objects/Objects.cpp#L72) | Перенесено з upstream. |
| F10 | Охорона і сирена авіаносця Vertical Bird | Є: [MissionRules.cpp:82](../../src/client/features/story/mission-rules/MissionRules.cpp#L82), [MissionRules.cpp:216](../../src/client/features/story/mission-rules/MissionRules.cpp#L216) | Перенесено з upstream. |
| F11 | Без дублювання машини біля входу | Є: [Vehicles.cpp:563](../../src/client/entities/vehicle/Vehicles.cpp#L563) | Перенесено з upstream. |
| F12 | Порожні машини без локальної землі не провалюються | Є: [Vehicles.cpp:1192](../../src/client/entities/vehicle/Vehicles.cpp#L1192), [Vehicles.cpp:967](../../src/client/entities/vehicle/Vehicles.cpp#L967) | Перенесено з upstream. |
| F13 | Камера водія при прицілюванні не повертається сама | Є: [Players.cpp:313](../../src/client/entities/player/Players.cpp#L313) | Перенесено з upstream. |
| F14 | Зброя у руці копії водія | Є: [Players.cpp:1715](../../src/client/entities/player/Players.cpp#L1715) | Перенесено з upstream. |
| F15 | Обличчя пасажира Wayfarer | Є: [Players.cpp:1558](../../src/client/entities/player/Players.cpp#L1558) | Перенесено з upstream. |
| F16 | Радіо не переходить у наступну машину і не заїкається | Є: [Radio.cpp:426](../../src/client/features/radio/Radio.cpp#L426) | Перенесено з upstream. |
| F17 | Власна дівчина у кожного, зокрема в машині | Є: [Peds.cpp:656](../../src/client/entities/ped/Peds.cpp#L656) | Перенесено з upstream. |
| F18 | Без перезавантаження світу під час більярду/підробітку | Є: [Flow.cpp:490](../../src/client/entities/session/Flow.cpp#L490) | Перенесено з upstream. |
| F19 | М’яч у баскетболі та автоматичний початок змагання | Є: [Contest.cpp:643](../../src/client/features/story/contest/Contest.cpp#L643) | Перенесено з upstream. |
| F20 | Без дублювання гантелей і штанги | Є: [Props.cpp:422](../../src/client/features/props/Props.cpp#L422) | Перенесено з upstream. |
| F21 | Поліцейська підробітка: гальмування і випадання мертвого водія | Є: [Peds.cpp:1089](../../src/client/entities/ped/Peds.cpp#L1089), [Vehicles.cpp:949](../../src/client/entities/vehicle/Vehicles.cpp#L949) | Збережено синхронізацію смерті та руху; виправлено повторні присвоєння, що скасовували згасання застарілого керування. |
| F22 | Посадка з води у Skimmer / Sea Sparrow | Є: [Players.cpp:398](../../src/client/entities/player/Players.cpp#L398) | Перенесено з upstream. |
| F23 | Відпущена після меню кнопка не спричиняє удар | Є: [Patches.cpp:50](../../src/client/app/patches/Patches.cpp#L50), [Input.cpp:26](../../src/client/shared/ui/input/Input.cpp#L26) | Перенесено з upstream. |

## Перевірка даних upstream

17 ключових файлів (нові модулі, радіо, гаражі, ефекти/об’єкти, каталог
анімацій та таблиця шляхів) побайтово збігаються з upstream. Файли зі змінами
форка звірено через `git diff upstream/v0.3.3 -- src`.

Таблиця містить 130 катсцен; додаткові шляхи: ранг 1: 130, ранг 2: 130.
У зібраному архіві є всі 12 пакетів IFP; їхні байти збігаються з upstream.

## Межі підтвердження

- Для 8 гравців перевіряється реальне ENet-підключення, місткість, повторний
  вхід, ретрансляція нових повідомлень і розділення балансів між іграми.
- Авторські шляхи катсцен для семи напарників у джерельному релізі відсутні.
  У форку перші три мають шляхи upstream; інші чотири отримують окремі безпечні
  місця. Твердження «усі сім ходять авторськими шляхами у 130 катсценах» було б неправильним.
- Перед релізом потрібен реальний прогін 8 копій GTA за
  [планом ігрової перевірки](upstream-0.3.3.md#required-gta-validation).

## Повторні перевірки після виправлень

- `python tools/build.py dist` — успішно; архів перебудовано з виправленого коду.
- `python tools/test_network_state.py` — успішно.
- `python tools/test_server_capacity.py` — успішно: 8 підключень, відмова 9-му,
  повторне використання слота, 45 типів повідомлень, старі протоколи,
  перемикання між окремими балансами ігор та правила сервера.
- `python tools/test_sync_primitives.py` — успішно.
- `python tools/fsd_check.py` та `python tools/i18n_check.py` — успішно.
- `node --test launcher/test/*.test.cjs` — 27/27.
- Перевірка цілісності `dist/SA-COOP.zip` — успішно.

## Повторна перевірка після 1ee38c3

Усі 40 пунктів повторно звірено з джерелами та інтеграцією. Збережено
побайтовий збіг 17 ключових файлів із upstream. Перевірено 106 знайдених
реєстрацій мережевих обробників: колізій числових ID та перезапису обробників
немає; обробники нових RC, гонок, більярду, змагань, анімацій, радіо,
переказів, зброї, телепортів, експорту та місійних ефектів підключені.

У N16 знайдено додаткову проблему порядку завантаження: `Economy::OnGameStarted`
міг застосувати попередній баланс до того, як `Progress` повідомив серверу ID
щойно завантаженої гри. Для хоста додано очікування правильного ID, а для
напарника — застосування профілю саме у поточному світі. При повторному
завантаженні того самого збереження ID гри надсилається повторно. До готовності
балансу перекази й адміністративне редагування не виконуються; вхідні перекази
очікують у наявній черзі.

Стан очікування реалізовано у
[CashReadiness.h](../../src/common/economy/CashReadiness.h), його використовує
[Economy.cpp](../../src/client/entities/economy/Economy.cpp). Регресійні сценарії
у [street_racing_test.cpp](../../tools/street_racing_test.cpp) охоплюють первинний
ID 0 перед отриманням ID збереження, стару відповідь сервера, повторне
завантаження тієї самої гри, очікування профілю напарника і повернення до старого
збереження без ID.

Після виправлення успішно повторено:

- `python tools/build.py dist`;
- `python tools/test_street_racing.py` з новими сценаріями готовності балансу;
- `python tools/test_sync_primitives.py`;
- `python tools/test_server_capacity.py`;
- `python tools/test_network_state.py`;
- `python tools/test_street_racing_network.py`;
- `python tools/fsd_check.py`, `python tools/i18n_check.py`;
- `node --test launcher/test/*.test.cjs` — 27/27;
- перевірку цілісності перебудованого `SA-COOP.zip`.

Це перевірка коду й автоматичних сценаріїв; ігровий прогін GTA та обмеження
катсцен для додаткових чотирьох напарників залишаються описаними вище.
