# Spike: il trascinamento dentro un tile (2026-10-18, Waveshare 7B, firmware 0.57.0)

| Domanda | Esito |
|---|---|
| Il numero segue il dito durante il trascinamento? | Sì: 36 eventi `spike value=` in una prova, a passi di 1 |
| `spike released` arriva al rilascio? | Sì (3 rilasci su 3 trascinamenti) |
| `on_tap` arriva dopo un trascinamento? | No: nessuna riga `on_tap` durante i trascinamenti |
| La pagina cambia se il dito esce dall'arco? | No: il core dice `edge swipe off: the touch starts on a slider` e non c'è cambio pagina |

Altri fatti: nessun `FAILED` del touch, nessun "short of memory", `psram_free=2755992`.
Un evento `swipe id=0 st=6 x=258 y=60` durante una prova: nessun cambio pagina visibile.

Conclusione: il trascinamento funziona nel tile; si prosegue col Task 2.
