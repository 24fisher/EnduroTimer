В EnduroTimerFirmware исправить потерю/задержку нажатий кнопок при подвисаниях OLED/LoRa/Wi-Fi.

Firmware version: 0.28

Проблема:
- На OLED периодически подвисают цифры времени.
- Если в этот момент нажать кнопку на StartStation или FinishStation, платы не реагируют.
- Значит main loop блокируется, и button.update() не вызывается вовремя.
- Нужно сделать ввод кнопок независимым от OLED/LoRa/Web/Wi-Fi.

Цель:
- Короткое нажатие кнопки не должно теряться, даже если main loop занят OLED redraw, LoRa TX, WebServer или HTTP.
- Время финиша должно фиксироваться по timestamp нажатия, а не по моменту поздней обработки события.

================================================================================
ЧАСТЬ 1. Добавить общий InputEventQueue
================================================================================

Добавить в common/hardware:

InputEvent.h
InputEventQueue.h / .cpp

Типы:

enum class InputEventType {
  StartButtonPressed,
  FinishButtonPressed
};

struct InputEvent {
  InputEventType type;
  uint32_t localMillis;
  uint32_t rawGpio;
};

Очередь:
- FreeRTOS QueueHandle_t
- размер 8 или 16 событий
- non-blocking push
- non-blocking pop

Если очередь заполнена:
- увеличить counter droppedInputEvents
- не блокировать InputTask

================================================================================
ЧАСТЬ 2. Добавить InputTask для кнопок
================================================================================

Создать common/hardware/ButtonInputTask.h/.cpp.

InputTask:
- FreeRTOS task
- priority выше OLED/Web
- period 2 ms или 5 ms
- debounce 30-50 ms
- active LOW
- фиксирует событие на debounced press edge, не на release
- не вызывает LoRa
- не вызывает OLED
- не вызывает Web
- не делает Serial spam
- не делает delay(), только vTaskDelayUntil()

Для StartStation:
- читает START_BUTTON_PIN=0
- создаёт InputEventType::StartButtonPressed

Для FinishStation:
- читает FINISH_BUTTON_PIN=0
- создаёт InputEventType::FinishButtonPressed

Task creation:
xTaskCreatePinnedToCore(...)

Рекомендуемо:
- InputTask stack 3072 или 4096
- priority 4 или 5
- core 1

================================================================================
ЧАСТЬ 3. Main loop должен обрабатывать события из очереди
================================================================================

В StartStationApp::loop():

- убрать зависимость старта от прямого polling wasShortPressed()
- в начале loop вызвать processInputEvents()
- если событие StartButtonPressed:
  - использовать event.localMillis для diagnostics
  - запустить start workflow, если состояние позволяет

В FinishStationApp::loop():

- в начале loop вызвать processInputEvents()
- если событие FinishButtonPressed:
  - если state == Riding:
    - зафиксировать finishLocalMillis = event.localMillis
    - вычислить finishRaceTimeMs по RaceClock из timestamp нажатия
    - resultMs = finishRaceTimeMs - activeRaceStartTimeMs
    - сохранить lastResult сразу
    - отправить FINISH
  - если state == AckTimeout:
    - manual resend same FINISH
  - не пересчитывать resultMs при resend

Важно:
- Событие кнопки не теряется, даже если main loop обработал его позже.
- Для спортивного результата использовать timestamp нажатия.

================================================================================
ЧАСТЬ 4. Добавить RaceClock conversion from captured millis
================================================================================

Если RaceClock умеет только nowRaceMs(), добавить метод:

uint32_t RaceClock::raceMsFromLocalMillis(uint32_t localMillis) const;

Логика:
- StartStation master: raceMs = localMillis или localMillis + offset, как сейчас устроено
- FinishStation slave: raceMs = localMillis + offsetToMasterMs

Нужно использовать тот же offset, что и nowRaceMs(), только не для текущего millis(), а для captured millis.

Это нужно, чтобы финишное время не зависело от задержки обработки события.

================================================================================
ЧАСТЬ 5. OLED вынести в throttled low-priority update, но не обязательно в отдельную task
================================================================================

Минимум:
- OLED render не чаще 5-10 Hz
- Riding elapsed line обновлять не чаще 200-300 ms
- Full redraw только если dirty
- Не делать OLED render каждый loop

Если делать OledTask:
- priority ниже InputTask
- не читать mutable app state напрямую
- получать DisplayState copy
- no business logic
- no LoRa/Web calls

Оставить HW I2C:
- U8G2_SSD1306_128X64_NONAME_F_HW_I2C
- Wire.setClock(400000)
- не возвращать software I2C

================================================================================
ЧАСТЬ 6. Добавить loop gap и input latency diagnostics
================================================================================

Логировать:

- maxLoopGapMs
- lastLoopGapMs
- inputEventQueueDepth
- droppedInputEvents
- inputCaptureToProcessLatencyMs
- maxInputCaptureToProcessLatencyMs

Если input latency > 100 ms:
Serial.printf("WARN input latency=%lu event=%s state=%s\n", ...);

Если loop gap > 200 ms:
Serial.printf("WARN loop gap=%lu lastBlock=%s\n", ...);

На OLED debug можно не выводить.

================================================================================
ЧАСТЬ 7. Не делать
================================================================================

- Не запускать LoRa из InputTask.
- Не запускать OLED из InputTask.
- Не запускать Web/HTTP из InputTask.
- Не делать обработчик кнопки через attachInterrupt с тяжёлой логикой.
- Не использовать delay() в task.
- Не дергать RadioLib из двух потоков одновременно.
- Не менять RaceClock sync architecture.
- Не менять LoRa protocol.
- Не менять RepeaterStation logic.
- Не ломать last result on FinishStation.
- Не возвращать long press.
- Не возвращать blocking LoRa poll.
- Не возвращать software I2C.

================================================================================
Ожидаемый результат
================================================================================

- При подвисании OLED цифр короткое нажатие кнопки всё равно фиксируется.
- Start button на StartStation не теряется.
- Finish button на FinishStation не теряется.
- Финишное время считается по моменту физического нажатия, а не по моменту обработки после подвиса.
- Кнопки работают даже во время LoRa TX / Web / OLED задержек.
- В логах видно input latency, если main loop тормозит.