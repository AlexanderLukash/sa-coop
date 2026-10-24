Мод для GTA San Andreas 1.0 US, с которым сюжет можно пройти вдвоём по сети.

## Что нового

- **Подработки и гонки вдвоём.** Такси, скорая, пожарный, полицейский, сутенёр, грузовой поезд, курьер и гонки можно проходить вместе. Кто начал занятие, нажимает Y, и второй игрок получает приглашение: «Ivan зовёт вас на совместное задание в такси». Он видит метки, чекпоинты и таймеры, может ехать рядом и помогать, а в конце получает ту же награду. Звать может и хост, и напарник.
- **Напарник в катсценах.** Напарник больше не пропадает в катсценах и стоит рядом с CJ. В поездках он садится в машину на свободное место. Если свободных мест нет, он стоит у машины.
- **Субтитры катсцен.** Напарник снова видит субтитры катсцен.
- **Брандмауэр.** Если друг видит «Сервер не отвечает», чаще всего его не пускает брандмауэр Windows у хоста. Теперь мод проверяет это сам, когда хост создаёт игру. Если доступ закрыт, Windows попросит разрешения. После него к хосту можно зайти и через интернет, и по локальной сети.
- **Антивирусы.** Файлы мода теперь содержат сведения о версии и собраны без отладочной информации, поэтому антивирусы должны реже принимать их за вирус. Если антивирус всё же удалит сервер мода или не даст ему запуститься, игра скажет об этом прямо.
- **Движения второго игрока.** Второй игрок двигается у вас так же, как у себя: входит в двери, приседает, делает перекаты из приседа в обе стороны, перезаряжает оружие, падает от удара и встаёт, забирается на уступы и раскрывает парашют. Сохраняются его стиль драки и походка толстого или накачанного CJ.
- **Напарник дальше 20 метров.** Напарник, который отошёл от хоста дальше 20 метров, больше не стоит на месте и не скользит за ним, а бежит. Его прыжки и подъёмы на стены на таком расстоянии тоже видны.
- **Точки начала миссий.** Точки начала миссий у напарника появляются и пропадают сразу вместе с хостом.
- **Сцены на движке игры.** Пока у хоста идёт сцена на движке игры (с чёрными полосами), напарник рядом с ним стоит на месте и не может её сорвать.
- **Прогресс побочных занятий.** У напарника сохраняется его прогресс в школах, подработках, спортзале, экспорте машин и трюковых прыжках, а также награды за них: машины у гаражей и доход с купленного имущества.
- **Фотоаппарат.** Снимки, которые напарник делает для миссии хоста, засчитываются в ней.
- **Мир.** Оба игрока видят выстрелы полицейского вертолёта, взрывчатку с детонатором, спуск спецназа по тросу, шары в бильярде и другие предметы побочных занятий. Поезда идут у обоих по одним путям. Полицейские машины остаются на месте после смерти хоста. Перемены на карте в миссиях хоста видны и напарнику, например поле конопли в «Are You Going To San Fierro?».
- **Настройки.** В `coop.ini` появились `partner_blip` и `name_tags` в разделе `[game]`: они выключают метку напарника на радаре и подписи над игроками.

## Исправления

- Подключение по коду больше не обрывается, пока загружается мир хоста.
- Исправлены вылеты при загрузке на сборках с CLEO, Project2DFX и другими модами, которые тоже рисуют поверх игры или перехватывают скрипты.
- Во время войны банд хоста напарник больше не получает звёзды розыска за каждое убийство.
- Процент прохождения у напарника больше не застывает на месте.
- Копия второго игрока, присевшая с оружием, больше не дрожит. Раньше перекат в одну из сторон превращался у неё в шаг в присяди.
- При выключенном уроне по напарнику его больше нельзя тихо убить ножом со спины.
- Напарник видит метку миссии Райдера у двери, а не далеко от неё. То же со всеми метками у входов в здания.
- Двери гаражей в миссиях хоста открываются и закрываются у обоих. Если миссия хоста убирает машину, в которой едет напарник, он выходит из неё, а не пропадает вместе с ней.
- Продавцы в магазинах и Каталина больше не появляются дважды.
- Копия второго игрока больше не стоит с раскинутыми руками, пока он играет в баскетбол или занимается в спортзале.
- Дым из мотора разбитой машины одинаковый у обоих игроков. Разворот машины на месте у второго игрока теперь плавный.
- Читы, которые меняют облик горожан, больше не переодевают второго игрока и не отбирают у него оружие ближнего боя.
- Свою погоду из своей миссии напарник больше не теряет из-за погоды хоста.
- Команды `/tp` и `/tphere` не срабатывают, пока кто-то из игроков в помещении.
- Если игра зависла на загрузке, в отчёт о зависании попадает то, что она пыталась загрузить.

## Установка

Распакуйте `SA-COOP.zip` в папку игры, чтобы `coop.asi`, `install.bat` и папка `coop` оказались рядом с `gta_sa.exe`, и запустите `install.bat`. Подробная инструкция лежит в архиве в файле `Инструкция.txt`, а описание мода в README репозитория.

Чтобы обновиться с прошлой версии, распакуйте архив поверх старых файлов. Настройки и прогресс напарника останутся, а `install.bat` запускать второй раз не нужно.

Версии 0.2.7 и 0.3.0 друг к другу не подключаются, поэтому обновиться нужно и хосту, и напарнику.

## In English

A mod for GTA San Andreas 1.0 US that lets two players go through the story together over the network.

### What's new

- **Odd jobs and races together.** Taxi, paramedic, firefighter, vigilante, pimp, freight train, courier and races can be done together. Whoever starts one presses Y, and the other player gets an invitation: "Ivan invites you to a joint taxi job". They see the markers, checkpoints and timers, can ride along and help, and get the same reward at the end. Both the host and the partner can invite.
- **The partner in cutscenes.** The partner no longer disappears in cutscenes and stands next to CJ. In scenes with a ride the partner takes a free seat in the car. If there is no free seat, the partner stands by the car.
- **Cutscene subtitles.** The partner sees cutscene subtitles again.
- **Firewall.** When a friend sees "Server not responding", it is most often Windows Firewall on the host's PC that keeps them out. The mod now checks this itself when the host creates a game. If access is closed, Windows asks for permission. Once it is given, friends can join over the internet and over a local network.
- **Antivirus software.** The mod's files now carry version information and are built without debug data, so antivirus software should flag them less often. If an antivirus still removes the mod's server or blocks it, the game says so plainly.
- **The other player's moves.** The other player moves on your screen the same way as in their own game: walks through doors, crouches, rolls from a crouch both ways, reloads, falls from a hit and gets up, climbs ledges and opens a parachute. Their fighting style and the walk of a fat or muscular CJ come along too.
- **A partner beyond 20 metres.** A partner more than 20 metres from the host no longer stands still and slides after them but runs. Jumps and wall climbs at that distance are seen too.
- **Mission start points.** The partner's mission start points appear and disappear together with the host's.
- **In-game scenes.** While the host has an in-game scene (with black bars), a partner nearby stands still and cannot break it.
- **Side activity progress.** The partner keeps their own progress in schools, odd jobs, gyms, car exports and stunt jumps, and the rewards for them: the cars at garages and the income from bought property.
- **Camera.** Photos the partner takes for the host's mission count in it.
- **The world.** Both players see the police helicopter's shots, remote explosives, SWAT officers sliding down ropes, pool balls and other objects of side activities. Trains run on the same tracks for both. Police cars stay where they are after the host's death. Map changes in the host's missions are seen by the partner too, for example the weed field in Are You Going To San Fierro?
- **Settings.** `coop.ini` has two new keys in `[game]`: `partner_blip` and `name_tags`. They turn off the partner's radar blip and the name tags above the players.

### Fixes

- Joining by code no longer drops while the host's world is loading.
- Fixed crashes on loading in modpacks with CLEO, Project2DFX and other mods that also draw over the game or hook its scripts.
- During the host's gang war the partner no longer gets wanted stars for every kill.
- The partner's completion percentage no longer stays frozen.
- The other player's copy no longer shakes when crouching with a gun. Before, a roll to one of the sides turned into a crouched step.
- With damage to the partner turned off, they can no longer be stealth-killed from behind with a knife.
- The partner sees Ryder's mission marker at the door, not far from it. The same goes for every marker at a building entrance.
- Garage doors in the host's missions open and close for both players. If the host's mission removes a car the partner rides in, the partner gets out instead of vanishing with it.
- Shop clerks and Catalina no longer appear twice.
- The other player's copy no longer stands with its arms spread while they play basketball or work out at the gym.
- The smoke from a damaged engine looks the same for both players. A car spinning on the spot turns smoothly for the other player now.
- Cheats that change the look of the city's people no longer change the other player's clothes or take their melee weapon.
- The partner no longer loses the weather of their own mission to the host's weather.
- The `/tp` and `/tphere` commands do nothing while either player is indoors.
- If the game hangs while loading, the hang report names what it was trying to load.

### Installing

Unpack `SA-COOP.zip` into the game folder so that `coop.asi`, `install.bat` and the `coop` folder sit next to `gta_sa.exe`, then run `install.bat`. The mod is described in detail in `README.en.md` in the repository.

To update from an earlier version, unpack the archive over the old files. Your settings and the partner's progress stay, and there is no need to run `install.bat` again.

Versions 0.2.7 and 0.3.0 cannot connect to each other, so both the host and the partner need to update.
