# Fast Track C600 su Apple Silicon

[English](README.en.md)

Progetto aperto per riportare la M-Audio Fast Track C600 su macOS moderno.
**Stato attuale: strumenti USB funzionanti; driver Core Audio ancora in sviluppo.**

```sh
make
./build/c600-probe
./build/c600-capture-check
./build/c600-capture-check --duplex
```

`c600-probe` legge descrittori e clock senza cambiare lo stato del dispositivo.
`c600-capture-check` seleziona temporaneamente il clock interno a 48 kHz,
riceve due secondi di pacchetti audio USB e mostra solo statistiche; non salva
l'audio. Con `--duplex` invia anche pacchetti di uscita silenziosi, sincronizzati
al conteggio dei campioni in ingresso. Richiede la C600 collegata e non deve
essere eseguito mentre un altro driver usa la scheda.

## Stato della diagnosi (27 ottobre 2026)

Mac di prova: MacBook Pro M3 Pro, macOS 15.6.1, architettura arm64.
Interfaccia: M-Audio Fast Track C600, USB VID:PID `0763:2031`, collegata tramite hub e alimentata solo via USB.

- macOS rileva il dispositivo USB, ma non lo elenca fra i dispositivi Core Audio.
- Nessun driver Avid/M-Audio risulta installato in `/Library/Audio/Plug-Ins/HAL` o `/Library/Extensions`.
- La configurazione USB ha interfacce audio con classe `0xff` (vendor-specific), anziché la classe USB Audio che il driver Apple associa automaticamente.
- Il descrittore indica streaming a 8 canali in uscita (`endpoint 0x01`, interfaccia 2, alternate setting 1) e 6 canali in ingresso (`endpoint 0x81`, interfaccia 3, alternate setting 1), PCM a 24 bit.
- Sono presenti anche una interfaccia MIDI standard (interfaccia 4), una HID (5) e una DFU (0). La presenza della DFU non dimostra che serva un aggiornamento firmware.
- La versione USB `bcdDevice=0x0017`, mostrata dal profilo sistema come `0.17`, non va interpretata senza documentazione come versione firmware.
- Lo strumento `c600-probe` ha letto il selettore clock su S/PDIF a 48 kHz prima dei test.
- `c600-capture-check` ha ricevuto 15.984 pacchetti, 1.726.812 byte, zero errori in due secondi: circa 1,999 s di audio 6 canali / 24 bit / 48 kHz. Non è stato registrato il contenuto audio.
- La prova duplex silenziosa ha ricevuto 15.992 pacchetti e inviato 15.968 pacchetti, senza errori. Il risultato si è ripetuto in tre esecuzioni consecutive.

Il kernel Linux include già il supporto per questa unità tramite una quirk USB audio: 8 uscite, 6 ingressi, 24 bit, 44,1/48/88,2/96 kHz e feedback implicito dal flusso di acquisizione. Questo conferma che il protocollo audio attuale è utilizzabile senza un firmware specifico per Apple Silicon.

## Direzione del progetto

1. Sviluppare un driver Core Audio per `0763:2031` che gestisca streaming USB isocrono in entrambe le direzioni e clock, inizialmente senza toccare il firmware.
2. Verificare sul dispositivo reale enumerazione, riproduzione, registrazione, cambio sample rate e riconnessione.
3. Aggiungere mixer, routing e controlli hardware dopo aver reso stabile lo streaming.
4. Valutare il firmware solo se emerge un limite dimostrato e dopo aver ottenuto immagine originale e procedura di recupero.

Una ricerca su GitHub del 27 ottobre 2026 non ha trovato un driver Core Audio pubblico per C600. Esiste [c600gui](https://github.com/obijywk/c600gui), un pannello di controllo, e [FastTrackC400](https://github.com/ruslanvakhitov/maudio_fast_track_c400), un driver per il modello C400. Entrambi non contengono una licenza esplicita: il loro codice non è incluso qui. Gli strumenti USB in questo repository sono originali e distribuiti sotto MIT.

## Fonti

- [Driver ufficiali Avid C600](https://kb.avid.com/pkb/articles/en_US/Knowledge/Fast-Track-C600-Drivers)
- [Quirk Linux per il C600](https://cos.googlesource.com/third_party/kernel/+/a65a6e1004cb6e445657dc2519022115fbde0c90/sound/usb/quirks-table.h)
- [Feedback implicito nel driver Linux](https://github.com/torvalds/linux/blob/master/sound/usb/implicit.c)
- [Manuale C600](https://device.report/m/7b0a5076eb6dae351f7a3bad5fb9359b5ef970348f67fc1a0edbd61313fba16d.pdf)
