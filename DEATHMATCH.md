# Deathmatch local para RigelEngine (Duke Nukem II)

Esta versión de RigelEngine añade un modo **deathmatch en pantalla dividida**
para 2 a 4 jugadores en el mismo PC. Es la primera etapa del proyecto. El modo
en red (cliente/servidor) vendrá después y se apoya en lo que hay aquí.

## Qué incluye

- **2 a 4 jugadores** con pantalla dividida: dos vistas lado a lado para 2
  jugadores, o una cuadrícula de 2×2 para 3 y 4.
- **Colores por jugador.** Duke 1 conserva sus colores originales. El 2 sale
  azulado, el 3 verdoso y el 4 amarillento.
- **Daño entre jugadores.** Los disparos de cada jugador dañan a los demás,
  pero nunca a quien disparó. El daño está ajustado para PvP:

  | Arma | Daño | Notas |
  |---|---|---|
  | Pistola | 1 | |
  | Láser | 2 | Atraviesa a los jugadores |
  | Cohete | 4 | |
  | Lanzallamas | 1 | Disparo continuo |

  Cada jugador tiene 9 puntos de vida.
- **Muertes y reaparición.** Al terminar la animación de muerte, el jugador
  reaparece con la vida llena y la pistola. El punto de aparición se elige
  lejos de los rivales, y hay unos instantes de invulnerabilidad al reaparecer.
  Solo se usan puntos a los que se puede llegar desde el inicio del nivel,
  nunca los huecos vacíos que hay fuera de las paredes.
- **Puntuación.** Cada baja suma 1 al jugador que dio el último golpe a la
  víctima. Eso incluye cuando la víctima termina muriendo por el entorno (lava,
  caídas, etc.) después de haber recibido un disparo. Si nadie le había dado,
  cuenta como muerte, pero no da ni quita puntos.
- **Rondas.** Gana la ronda el primero que llega al límite de bajas (10 por
  defecto). A los 6 segundos empieza una ronda nueva.
- **Reaparición de objetos.** Las armas, objetos y cajas que se recogen o
  destruyen vuelven a aparecer en su sitio a los 20 segundos.
- **Mapas sin enemigos.** Se quitan los enemigos del mapa (se pueden conservar
  con `--keep-enemies`). Los peligros del escenario, como fuego, pinchos o
  láseres fijos, siguen activos y dañan a todos.
- **Marcador.** Cada vista muestra arriba la vida, las bajas, el arma y la
  munición de su jugador. Debajo de las vistas aparece el marcador general
  (bajas/muertes de cada jugador).

## Controles

| Acción | Jugador 1 | Jugador 2 | Jugadores 3 y 4 |
|---|---|---|---|
| Moverse | Flechas (o las teclas que tengas configuradas) | W A S D | Mando 3 / Mando 4 |
| Saltar | Ctrl | G | Mando |
| Disparar | Alt | F | Mando |
| Interactuar (puertas, teletransportes…) | Flecha arriba | W | Mando |
| Mando | Primer mando conectado | Segundo mando | Tercer y cuarto mando |

- El jugador 1 usa las teclas configuradas en el menú de opciones del juego.
  Si las cambiaste a W/A/S/D, chocarán con las del jugador 2.
- **Esc** abre el menú del juego, que sirve para salir.
- Los mandos se asignan en el orden en que Windows los detecta. Conéctalos
  **antes** de abrir el juego.

## Cómo arrancarlo

Desde una consola (`cmd` o PowerShell), dentro de la carpeta donde está
`RigelEngine.exe`:

```
RigelEngine.exe --deathmatch -l L1 "C:\ruta\a\DUKE2"
```

La última parte es la carpeta con los archivos del juego original (más abajo
se explica cómo conseguirlos). Si copias `RigelEngine.exe` y sus DLL a esa
misma carpeta, puedes omitirla.

### Opciones

| Opción | Qué hace | Por defecto |
|---|---|---|
| `--deathmatch` | Activa el modo deathmatch | — |
| `-l L1` … `-l L8` | Mapa a usar. Episodio 1 = `L`; los episodios 2, 3 y 4 = `M`, `N`, `O` (solo con la versión registrada) | `L1` |
| `--players 3` | Número de jugadores, de 2 a 4 | 2 |
| `--frag-limit 15` | Bajas necesarias para ganar la ronda | 10 |
| `--item-respawn 30` | Segundos hasta que reaparecen los objetos | 20 |
| `--keep-enemies` | No quita los enemigos del mapa | desactivado |
| `--difficulty hard` | Dificultad (cambia qué objetos y peligros hay en el mapa) | medium |

Ejemplo con 4 jugadores en el mapa 3, a 15 bajas:

```
RigelEngine.exe --deathmatch --players 4 --frag-limit 15 -l L3
```

**Recomendación:** activa el modo panorámico (widescreen) en las opciones del
juego. Las vistas divididas quedan mucho más anchas.

## Conseguir los datos del juego

RigelEngine necesita los archivos del Duke Nukem II original. La versión
shareware (episodio 1, mapas `L1` a `L8`) es gratuita y legal:

<https://archive.org/download/msdos_DUKE2_shareware/DUKE2.zip>

Descomprímela en una carpeta, por ejemplo `E:\Juegos\DUKE2`. Solo hacen
falta `NUKEM2.CMP` (imprescindible: mapas, gráficos y sonido) y `NUKEM2.F1` a
`NUKEM2.F5` (animaciones de la intro). Los archivos `NUKEM2.-*` son partidas
guardadas y récords del juego original: RigelEngine los importa si están, pero
son opcionales.

## Partida en red

Un jugador crea la partida (host) y los demás se conectan a su IP. Se puede
entrar y salir en cualquier momento.

**Crear la partida** (por defecto hasta 4 jugadores, contándote a ti):

```
RigelEngine.exe --host -l M1 "E:\Juegos\DUKE2"
```

**Unirse** (el nivel lo decide el host):

```
RigelEngine.exe --connect 192.168.1.20 "E:\Juegos\DUKE2"
```

Cambia `192.168.1.20` por la IP del host. Para saber tu IP en la red de tu
casa, ejecuta `ipconfig` en una consola y busca "Dirección IPv4". Para
probarlo en un solo PC, abre dos copias del juego y conecta la segunda a
`127.0.0.1`.

| Opción | Qué hace | Por defecto |
|---|---|---|
| `--host` | Crea una partida en red | — |
| `--connect <ip>` | Se une a una partida (`ip` o `ip:puerto`) | — |
| `--port <número>` | Puerto UDP del host | 7777 |
| `--simulate-lag <ms>` | Solo cliente, para pruebas: añade ese retraso (ida y vuelta) a la conexión | 0 |
| `--simulate-jitter <ms>` | Solo cliente, para pruebas: hace que el retraso varíe al azar hasta esa cantidad, como en una conexión real irregular | 0 |
| `--no-lag-compensation` | Solo host: los impactos se juzgan por dónde está cada jugador en el host, no por lo que vio quien disparó | (activada) |
| `--players <2-4>` | Plazas en total, incluido el host | 4 |
| `--frag-limit`, `--item-respawn`, `--keep-enemies`, `--difficulty` | Igual que en local; solo las usa el host | |

Cosas a tener en cuenta:

- **Todos necesitan los mismos archivos de datos del juego** (y el mismo
  `M1.MNI` si se juega en la arena) y **la misma versión de RigelEngine**.
  Si no, el host rechaza la conexión o los gráficos salen mal.
- **Cortafuegos de Windows:** la primera vez que hagas de host, Windows te
  preguntará si permites que RigelEngine use la red. Acepta, al menos para
  redes privadas.
- **Por Internet (fuera de tu casa):** el host tiene que abrir/redirigir el
  puerto **7777 UDP** en su router hacia su PC, y los demás se conectan a su
  IP pública. Alternativa sin tocar el router: una red privada virtual tipo
  ZeroTier, Tailscale o Radmin VPN, y usar la IP que te da esa red.
- **Cómo funciona:** el host ejecuta la partida y decide todo lo que importa:
  disparos, daño, muertes, objetos y reapariciones. Cada cliente simula su
  propio Duke en su PC (predicción), así que **tu personaje responde al
  instante aunque haya ping alto**. Si la simulación del cliente no coincide
  con la del host (por ejemplo, el host vio un ventilador que te empujó o una
  puerta cerrada), el cliente se corrige solo.
- **Movimiento suave de los demás (interpolación):** por Internet los datos
  del host no llegan a ritmo constante: a veces llegan dos juntos y a veces
  ninguno. El cliente guarda unos pocos y muestra a los demás jugadores,
  enemigos y disparos un poquito en el pasado (normalmente unos 130 ms), a
  ritmo fijo, deslizándolos entre una posición y la siguiente. El retraso se
  ajusta solo: más si la conexión es irregular, menos si es estable. Tu
  propio Duke no se ve afectado, sigue respondiendo al instante.
- **Compensación de lag:** como ves a los demás un poco en el pasado, el
  host juzga tus disparos según lo que veías tú en pantalla al disparar, no
  según dónde estaba el rival cuando le llegó tu disparo. Si le diste en tu
  pantalla, le diste. Tiene un límite de 0,8 segundos: con más retraso que
  eso tienes que adelantar el tiro. La contrapartida, como en cualquier
  juego con compensación, es que a veces te pueden dar justo después de
  ponerte a cubierto.
- **El host se adapta a conexiones irregulares:** guarda las entradas de
  cada cliente que llegan a tirones, pero si durante un rato sobran, las
  gasta más rápido para no acumular retraso.
- **Probar el retraso sin Internet:** añade `--simulate-lag 200` al cliente
  para simular 200 ms de ping, y `--simulate-jitter 80` para que además
  varíe, por ejemplo al conectarte a `127.0.0.1`. El archivo de log del
  cliente muestra cada 5 segundos una línea "Network smoothness" con cuántos
  tirones habría habido sin interpolación ("received") y cuántos se ven
  ("shown").
- **Si el host abre el menú (Esc), la partida se pausa para todos.**
- **Un cliente que sale al menú principal abandona la partida.** Para
  volver a entrar, reinicia el juego con `--connect`.
- **Si alguien pierde la conexión,** el host lo saca de la partida a los
  pocos segundos y su plaza queda libre.

## Mapa de prueba: la arena

`tools/deathmatch_arena/make_arena.py` genera una arena pensada para
deathmatch:

- Una sala de 64×40 tiles con suelo, 4 alturas de plataformas, armas
  (láser, lanzallamas, lanzacohetes y disparo rápido) y salud.
- El jugador 1 empieza a la izquierda.
- Usa los gráficos de L1, que están también en la versión shareware.
- Está comprobado con la física real de Duke: se puede llegar saltando a
  todas las plataformas.

Para generarla e instalarla, con Python 3:

```
python tools\deathmatch_arena\make_arena.py E:\Juegos\DUKE2\M1.MNI
```

Para jugarla: `--deathmatch -l M1 "E:\Juegos\DUKE2"`

Se guarda como `M1.MNI` a propósito. La versión shareware no tiene mapa
`M1`, así que la arena aparece como un mapa extra y no reemplaza ningún
mapa original. Con la versión completa del juego, dale otro nombre (por
ejemplo `O8.MNI`) para no tapar un mapa existente. RigelEngine usa siempre
un archivo `.MNI` suelto en la carpeta del juego antes que el que va dentro
de `NUKEM2.CMP`.

Para cambiar la arena, edita las listas `PLATFORMS` y `ACTORS` del script
y vuelve a generarla. Duke salta como máximo 7 tiles de alto, así que deja
como mucho 6 filas entre plataformas.

## Compilar en Windows

### Opción A: Visual Studio en tu PC

Probado con Visual Studio 2026 Community.

1. Instala **Visual Studio** con la carga de trabajo *"Desarrollo para el
   escritorio con C++"*. Por defecto ya incluye el compilador MSVC, el SDK de
   Windows y las herramientas de CMake, así que no hace falta instalar CMake
   aparte. También necesitas **Git**: márcalo en "Componentes individuales"
   ("Git para Windows") o instálalo desde <https://git-scm.com/download/win>.
2. Abre **"Developer PowerShell for VS 2026"** (o la versión que tengas) desde
   el menú Inicio. Haz todo lo que sigue en esa consola.
3. Instala **vcpkg** y las librerías SDL2 (una sola vez):
   ```
   cd C:\
   git clone https://github.com/microsoft/vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg install sdl2:x64-windows sdl2-mixer:x64-windows
   ```
4. Descomprime el código en una carpeta sin espacios ni tildes en la ruta, por
   ejemplo `E:\RigelEngine-deathmatch`, y genera la solución de Visual Studio:
   ```
   cd E:\RigelEngine-deathmatch
   mkdir build
   cd build
   cmake .. -A x64 "-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=x64-windows" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
   ```
   - **Las comillas son necesarias en PowerShell.** Sin ellas, PowerShell
     corta `3.5` en el punto y CMake falla con *"Invalid
     CMAKE_POLICY_VERSION_MINIMUM value 3"*. Si te pasa, borra la carpeta
     `build` antes de repetir el comando.
   - `CMAKE_POLICY_VERSION_MINIMUM` hace falta con CMake 4, que es el que
     trae VS 2026, porque una librería incluida (entityx) pide una versión de
     CMake muy antigua.
   - Los avisos amarillos sobre `SDL2main` y entityx son normales.
5. Abre `build\RigelEngine.slnx` (o `.sln`) en Visual Studio y:
   - Cambia la configuración a **Release**.
   - Clic derecho en **RigelEngine** → *Establecer como proyecto de inicio*.
   - Clic derecho en **RigelEngine** → *Propiedades* → *Depuración* →
     *Argumentos de comando*: `--deathmatch -l L1 "E:\Juegos\DUKE2"`
   - **Ctrl+Shift+B** compila y **F5** ejecuta.
6. El ejecutable queda en `build\src\Release\RigelEngine.exe`, junto con
   `SDL2.dll` y `SDL2_mixer.dll`. Para llevarlo a otro PC, copia los tres.

Para editar el código, abre los archivos desde el Explorador de soluciones y
vuelve a compilar. Si añades un archivo `.cpp` nuevo, agrégalo a
`src/CMakeLists.txt`. Visual Studio vuelve a ejecutar CMake solo al compilar.

### Opción B: que GitHub lo compile por ti (sin instalar nada)

El código incluye un workflow de GitHub Actions
(`.github/workflows/windows-deathmatch.yml`) que compila el `.exe` para
Windows en los servidores de GitHub.

1. Crea un repositorio en tu cuenta de GitHub y sube este código (o súbelo a
   tu fork de RigelEngine, en la rama `deathmatch`).
2. En GitHub, abre la pestaña **Actions**. En los forks, GitHub las trae
   desactivadas: pulsa el botón verde para activarlas. Luego elige
   **Windows build (RigelNet)** → **Run workflow**.
3. Cuando termine (unos 10–20 minutos), descarga el artefacto
   **RigelNet-windows** al final de la página del workflow. Trae
   el `.exe`, las DLL y esta guía.

Este workflow no se ha ejecutado todavía. Si falla, el registro de GitHub
indica en qué paso; pásame ese error.

## Limitaciones conocidas

- **Algunos objetos del mapa solo reaccionan al jugador 1:** ascensores,
  la nave espacial, el campo de fuerza especial que mata al tocarlo y la
  salida del nivel (la salida, de todos modos, está desactivada en
  deathmatch).
- **Enemigos con `--keep-enemies`.** Si los conservas, solo persiguen y
  atacan al jugador 1. Sin esa opción se quitan todos los de una lista fija.
  Los peligros del escenario (fuego, pinchos, martillo, etc.) se quedan y
  dañan a todos.
- **Recoger un dispositivo de camuflaje** (cloak) o de disparo rápido reinicia
  el temporizador de ese objeto para todos los jugadores que ya lo tengan.
- **Al disparar se bloquea un momento la opción de mirar arriba o abajo**
  (desplazar la cámara a mano) para todos los jugadores, no solo para quien
  dispara.
- **Los efectos de agua y camuflaje** podrían verse algo raros en pantalla
  dividida. No se ha podido comprobar todavía.
- **El modo de juego "clásico"** de las opciones no admite deathmatch. Si lo
  tienes activado, el deathmatch usa el modo moderno automáticamente.
- **Guardado rápido desactivado** durante el deathmatch.
- **Si sales al menú principal** y empiezas partida nueva, sigue en
  deathmatch mientras el juego se haya abierto con `--deathmatch`.

## Para quien quiera tocar el código

| Archivo | Contenido |
|---|---|
| `src/game_logic/game_world_deathmatch.cpp` | Toda la lógica del modo: alta de jugadores, reglas, daño entre jugadores, aparición, objetos y marcador |
| `src/game_logic/deathmatch_state.hpp` | Estado de la partida y de cada jugador |
| `src/game_logic/deathmatch_spawn_points.cpp` | Elige los puntos de aparición: solo zonas del nivel alcanzables desde el inicio |
| `src/game_logic_common/deathmatch_options.hpp` | Opciones del modo |
| `src/game_logic/game_world.cpp` | Ramas de deathmatch en el renderizado y los eventos |
| `src/frontend/input_handler.cpp` | Controles por jugador (teclado y mando) |
| `src/frontend/game_runner.cpp` | Crea los controles de cada jugador y reparte la entrada |
| `src/main.cpp` | Opciones de línea de comandos |
| `src/game_logic/camera.cpp` | La cámara se adapta a vistas pequeñas |
| `src/engine/entity_activation_system.cpp`, `src/engine/life_time_system.cpp` | Soporte para varias vistas simultáneas |
| `src/engine/sprite_rendering_system.cpp` | Teñido de sprites (colores de jugador) |
| `src/game_logic/game_world_network.cpp` | Lado del juego en red: capturar (host) y aplicar (cliente) instantáneas |
| `src/game_logic_common/network_world.hpp` | Datos que el host manda a los clientes |
| `src/network/` | Protocolo de red y conexión (sobre ENet) |
| `src/frontend/network_session.cpp` | Une la red con el bucle del juego: altas, bajas, entradas y envíos |
| `src/game_logic/snapshot_interpolation.cpp` | Interpolación en el cliente: muestra lo que manda el host a ritmo constante |
| `3rd_party/enet/` | Librería de red ENet (licencia MIT), incluida como código fuente |
| `test/test_multiplayer.cpp` | Tests de lo anterior |
| `test/test_network_protocol.cpp` | Tests del protocolo de red |
| `test/test_snapshot_interpolation.cpp` | Tests de la interpolación y la compensación de lag |
| `tools/deathmatch_arena/make_arena.py` | Generador de la arena de prueba |

El modo de un jugador funciona exactamente igual que antes. Todas las
ramificaciones nuevas solo se activan con `--deathmatch`.

Opciones de depuración: `--debug-input-script <teclas>` reemplaza el teclado
por una secuencia que se repite (una letra por fotograma: `L`/`R` izquierda y
derecha, `U`/`D` arriba y abajo, `J` saltar, `F` disparar, otra cosa = nada).
Sirve para probar el modo en red de forma automática.

Siguientes pasos posibles: crear y unirse a partidas desde el menú del juego,
y que ascensores y nave funcionen para todos los jugadores.

## Licencia

RigelEngine se distribuye bajo GPL-2.0, y estas modificaciones también. Si
publicas binarios, tienes que publicar también el código fuente.
