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

Descomprímela en una carpeta, por ejemplo `C:\Juegos\DUKE2`. Debe contener,
entre otros, el archivo `NUKEM2.CMP`.

## Compilar en Windows

### Opción A: Visual Studio en tu PC

1. Instala:
   - **Visual Studio 2022 Community** (gratis), con la carga de trabajo
     *"Desarrollo para el escritorio con C++"*.
   - **CMake**: <https://cmake.org/download/> (instalador *Windows x64*;
     marca la opción de añadirlo al PATH).
   - **Git**: <https://git-scm.com/download/win>
2. Instala **vcpkg**, el gestor de librerías, desde una consola:
   ```
   cd C:\
   git clone https://github.com/microsoft/vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   C:\vcpkg\vcpkg install sdl2:x64-windows sdl2-mixer:x64-windows
   ```
3. Descomprime el código de este zip, por ejemplo en `C:\RigelEngine`.
4. Genera el proyecto de Visual Studio:
   ```
   cd C:\RigelEngine
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -A x64
   ```
5. Compila en modo Release (puedes hacerlo desde la consola, no hace falta
   abrir Visual Studio):
   ```
   cmake --build . --config Release --target RigelEngine
   ```
6. El ejecutable queda en `C:\RigelEngine\build\src\Release\RigelEngine.exe`,
   junto con `SDL2.dll` y `SDL2_mixer.dll`.

Si CMake dice que no encuentra SDL2, casi siempre es que la ruta de
`CMAKE_TOOLCHAIN_FILE` está mal. Tiene que ser absoluta.

### Opción B: que GitHub lo compile por ti (sin instalar nada)

El código incluye un workflow de GitHub Actions
(`.github/workflows/windows-deathmatch.yml`) que compila el `.exe` para
Windows en los servidores de GitHub.

1. Crea un repositorio en tu cuenta de GitHub y sube este código (o súbelo a
   tu fork de RigelEngine, en la rama `deathmatch`).
2. En GitHub, abre la pestaña **Actions** y elige **Windows deathmatch
   build** → **Run workflow**.
3. Cuando termine (unos 10–20 minutos), descarga el artefacto
   **RigelEngine-deathmatch-windows** al final de la página del workflow. Trae
   el `.exe`, las DLL y esta guía.

Este workflow no se ha ejecutado todavía. Si falla, el registro de GitHub
indica en qué paso; pásame ese error.

## Limitaciones conocidas

- **Algunos objetos del mapa solo reaccionan al jugador 1:** ascensores,
  la nave espacial, el campo de fuerza especial que mata al tocarlo y la
  salida del nivel (la salida, de todos modos, está desactivada en
  deathmatch).
- **Algunos enemigos pueden quedar.** Se quitan los que aparecen en el radar,
  que son casi todos. Si queda alguno, solo persigue y ataca al jugador 1.
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
| `src/game_logic_common/deathmatch_options.hpp` | Opciones del modo |
| `src/game_logic/game_world.cpp` | Ramas de deathmatch en el renderizado y los eventos |
| `src/frontend/input_handler.cpp` | Controles por jugador (teclado y mando) |
| `src/frontend/game_runner.cpp` | Crea los controles de cada jugador y reparte la entrada |
| `src/main.cpp` | Opciones de línea de comandos |
| `src/game_logic/camera.cpp` | La cámara se adapta a vistas pequeñas |
| `src/engine/entity_activation_system.cpp`, `src/engine/life_time_system.cpp` | Soporte para varias vistas simultáneas |
| `src/engine/sprite_rendering_system.cpp` | Teñido de sprites (colores de jugador) |
| `test/test_multiplayer.cpp` | Tests de lo anterior |

El modo de un jugador funciona exactamente igual que antes. Todas las
ramificaciones nuevas solo se activan con `--deathmatch`.

Siguiente paso previsto: el modo en red cliente/servidor, con predicción del
jugador local para eliminar la sensación de retraso.

## Licencia

RigelEngine se distribuye bajo GPL-2.0, y estas modificaciones también. Si
publicas binarios, tienes que publicar también el código fuente.
