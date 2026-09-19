# HTNHotReloadDemo

Playground aislado de hot reload del dominio Wanderer, reutilizando los NPC,
terreno y panel de simulación de HTNDemo. Primera versión: Windows x64, VS2022
con herramientas de C++ (incluido Windows SDK). No modifica la ABI del planner.

## Arranque

1. Superponer los ficheros del parche sobre la versión actual del proyecto.
2. Ejecutar `GenerateProjectFiles.bat` para regenerar la solución VS2022.
   Si usabas opciones de Premake, mantenerlas al regenerar.
3. Seleccionar `HTNHotReloadDemo` como proyecto de inicio y compilar en `Profile`
   (también admite Debug, ProfileDetailed y Release). Las dependencias generan
   el primer dominio y compilan el translator y el runtime bridge.
4. Ejecutar el proyecto. Deben aparecer ocho NPC y el estado `Initial build active`.

No hacer Build/Rebuild desde Visual Studio con la demo abierta: la DLL inicial
está cargada y Windows impide sobrescribirla. Para iterar, usar sus botones.

## Flujo

- `Domain Source`: editar `Domains/Wanderer.domain` y pulsar `Save Domain`.
  `Discard edits / Read from disk` vuelve a leer el fichero. Los includes, como
  `Domains/Includes/movement2.domain`, se pueden editar externamente.
- `Compile Domain`: ejecuta HTNTranslator de la misma configuración y después
  MSVC sobre el C generado. Produce una DLL candidata **de nombre fijo** en
  `bin/<config>-windows-x86_64/HTNHotReloadDemo/candidate/WandererHTN.dll`.
  Los NPC continúan usando la DLL activa. El resultado y los diagnósticos se
  muestran en `Compiler Output` (la vista limita el texto a 256 KiB; el log en
  disco conserva la salida completa).
- `Hot Reload`: entre updates, libera las unidades y el almacenamiento preparado
  de todos los NPC, descarga la DLL activa, sustituye `WandererHTN.dll` con el
  candidato y valida/carga la nueva definición. **No ejecuta HTNTranslator**.
  Solo aplica el último candidato compilado correctamente. Para incluir cambios
  externos posteriores hay que volver a pulsar Compile.
- Conserva posiciones, edad, velocidad, estado de juego, WorldState, hooks,
  daemons, selección y estadísticas históricas. Descarta los planes activos,
  temporizadores de primitives y capturas del debugger. Replanifica en el
  siguiente update; el scratch de ejecución de cada unidad se inicializa entonces.
- Antes de descargar guarda una copia fija en `previous/WandererHTN.dll`.
  Si falla la recarga, restaura esa DLL y reconstruye los planners. Si falla la
  restauración o no había dominio activo, mantiene la simulación sin planner y
  pausada hasta que se compile/recargue una DLL válida. Un fallo al hacer backup
  deja intacto el dominio activo.

Guardar cambios invalida el candidato; con cambios sin guardar los botones están
deshabilitados. Compilar no ejecuta callterms del candidato. La recarga comprueba
ABI/export y lifecycle; no demuestra que el nuevo dominio encuentre un plan con
todos los WorldStates. Un fallo de planificación se observa en el panel de NPC.

El runtime bridge permanece cargado durante toda la sesión. Solo se descarga la
DLL del dominio. El binding global se configura una vez; cada NPC sigue teniendo
su propio contexto de daemons y PlannerHook. No se usa el intérprete en esta demo.

## Comprobación rápida

Desde la raíz, una vez compilado Profile:

```bat
bin\Profile-windows-x86_64\HTNHotReloadDemo\HTNHotReloadDemo.exe --self-test
```

Comprueba con las DLL reales ocho recargas **durante una primitive de movimiento,
con deferred calls pendientes**, conservación de WorldState/posición/
edad/velocidad, eliminación del plan anterior, replanning, rollback ante un fichero
DLL inválido y update seguro sin dominio. No abre ventana ni modifica los domains;
solo usa artefactos del directorio de binarios. Guarda la DLL original en
`self-test-original/WandererHTN.dll` y la restaura al terminar, incluso ante un
fallo normal de las comprobaciones. Si falla la copia de restauración, devuelve
error y muestra la ruta desde la que recuperarla. Cerrar
otras instancias de la demo antes de ejecutarlo. Debe terminar con
`Hot reload self-test: PASS` y código 0.

También exige facts publicados al inicializar y movimiento/ejecución real de tasks:
que la rama fallback idle devuelva un plan válido no basta para pasar. La demo
registra al crear cada hook los siete facts que publican sus daemons, sin depender
de parsear el source editable. Ese registro permanece válido durante las recargas.

## Validación completa de Compile + Hot Reload (Windows)

```bat
bin\Profile-windows-x86_64\HTNHotReloadDemo\HTNHotReloadDemo.exe --pipeline-self-test
```

Incluye la comprobación anterior y usa **la misma ruta asíncrona de Compile que
el botón**, con un source de prueba aislado en `pipeline-fixture/Wanderer.domain`:

1. Copia el dominio de `HTNHotReloadDemo/Validation` y el include de movimiento al
   directorio de binarios. No modifica tus sources originales.
2. Compila la revisión A y luego B. Comprueba que Compile no cambia la definición,
   revisión ni contenido de la DLL activa y que los NPC siguen actualizándose.
3. Verifica que el comportamiento nuevo solo aparece después de Hot Reload,
   mediante las primitives `!say "reload-validation-A"` y `!say "reload-validation-B"`.
4. Introduce un error de sintaxis en la copia. Exige un fallo del compilador,
   candidato no disponible, Hot Reload rechazado y dominio/NPC activos intactos.
5. Recarga ocho veces durante movimiento con deferred calls pendientes, verifica
   que el movimiento se retoma y que se ejecuta el comentario deferred de B
   **después** de esas recargas. Prueba también rollback y movimiento posterior.
6. Descarga los módulos y restaura la DLL original antes de devolver éxito.

Salida final esperada: `Hot reload pipeline self-test: PASS`, código 0. Los errores
del compilador durante la prueba de source inválido son intencionados; no deben
hacer fallar el test. No requiere ventana ni GitHub Actions. Ejecutar con todas
las instancias de la demo cerradas y sin builds concurrentes. La prueba espera
a que termine el compilador; no tiene timeout/cancelación.

En plataformas sin MSVC este modo devuelve 2 y dice explícitamente que no se ha
ejecutado. El `--self-test` básico no necesita invocar al compilador.

Prueba manual: cambiar un texto de `!say`, guardar, Compile (el dominio activo no
cambia), Hot Reload y comprobar el nuevo texto cuando se alcance esa rama. Luego
introducir un error de sintaxis y comprobar que Compile falla sin interrumpir los
NPC. Deshacer el error y repetir.

## Límites deliberados

Sin watcher, recarga automática, migración de planes ni compilación remota. El
build corre en un hilo; cerrar la aplicación espera a que termine, sin timeout ni
cancelación. Cambiar el ABI/runtime, flags de instrumentación o firmas/bindings
de C++ exige cerrar y recompilar los proyectos correspondientes, no solo el domain.
No es una transacción resistente a caídas del proceso ni un sandbox para DLLs
no confiables. Una falta de memoria en la preparación por NPC se rige por los
mecanismos de allocation existentes del planner; no se añade otra capa de allocator.
