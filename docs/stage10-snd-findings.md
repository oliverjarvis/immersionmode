# nn::snd in Pokémon Ultra Moon USA (code at 0x100000)

Source: a reverse-engineering agent, 2026-10-12. Confidence is high unless marked.

## Key addresses (VA, ARM)

| What | Address |
|---|---|
| CriticalSection Enter / Leave | 0x1076a8 / 0x107730 |
| nn::snd sound-thread singleton | 0x6b4628 (0x25bfe0 returns it) |
| **Sound-thread Lock()** (recursive) | **0x25cac0** |
| **Sound-thread Unlock()** | **0x25d418** |
| VoiceManager | 0x6b4678 (its lock at +0x10) |
| Voice table | Voice*[24] at 0x6b5c54 |
| nn::os device memory | {u32 addr, u32 size} at 0x690514 / 0x690518 |
| Linear VA→PA offset | 0x69ca4c |
| **VA→DSP address, device memory only** | **0x172be4**: `u32 f(u32 va, u32 bytes)`, returns 0 if outside [devmem, devmem+size), else va + *(u32*)0x69ca4c |
| FlushDataCache(addr, bytes) | 0x1756c8 (DSP request 0x00130082). The library never flushes by itself. |

## Exported API (VA in symbols.json)

The Voice methods are thin wrappers: `impl = *(Voice+0x68)`.

- **AllocVoice** (0x25aa68)
  - Priority is 0..0x7FFF; NULL on failure.
  - 24 voices exist. It preempts the lowest-priority voice even at equal priority. Priority 0x7FFF is never preempted or dropped.
  - The callback means "your voice was dropped". It runs on the sound thread, after the voice has been freed.
- **FreeVoice** (0x25fc38): stops the voice, unlinks it, and does not call the callback.
- **A new voice's defaults**:
  - FL = FR = 1.0, volume 1.0 (not silent);
  - mono PCM16, rate 32728 Hz;
  - state **PAUSE (2)**: the caller must SetState(PLAY = 0).
- **Enums**:
  - SampleFormat: PCM8 = 0, PCM16 = 1, ADPCM = 2.
  - State: PLAY = 0, STOP = 1 (marks every buffer DONE), PAUSE = 2.
- **MixParam**: 12 floats, `mainBus[4]` (FL, FR, RL, RR), `auxBusA[4]`, `auxBusB[4]`.
- **WaveBuffer**, 0x18 bytes:

  ```c
  typedef struct WaveBuffer {
    const void *bufferAddress;   // +0x00 VA, must pass 0x172be4 (device memory)
    s32 sampleLength;            // +0x04 samples
    const void *pAdpcmContext;   // +0x08
    u32 userParam;               // +0x0C
    u8 loopFlag;                 // +0x10
    volatile u8 status;          // +0x11 FREE=0 WAIT=1 PLAY=2 DONE=3
    u16 bufferId;                // +0x12 set by Append
    struct WaveBuffer *next;     // +0x14 owned by the library
  } WaveBuffer;
  ```

  - Append checks nothing. Only append buffers whose status is 0 or 3. A length of 0 goes straight to DONE.
  - A bad address fails silently: the voice stalls.
  - The DSP holds at most 5 buffers: the first plus a 4-slot queue. A ring that runs dry gives a small gap, then restarts on the next Append.

## Threads

- The voice functions take no global lock. The sound thread's frame (loop at 0x25c370) does three things:
  1. WaitForDspSync, which updates the buffer statuses;
  2. the game's frame callback, under Lock;
  3. SendParameterToDsp, under Lock. It has exactly one caller, 0x25c4a0.
- **Every payload voice call goes between Lock (0x25cac0) and Unlock (0x25d418).** The game's own main thread does the same.
- Polling `status` needs no lock; read it as volatile.

## Progress

`impl = *(void**)(voice+0x68)`:

| Field | Meaning |
|---|---|
| `*(u32*)(impl+0x08)` | sample position within the current buffer |
| `*(u8*)(impl+0x0C)` | DSP active |
| impl+0x30 | queue head |
| `*(s16*)(impl+0x34)` | buffers handed to the DSP |

## The memory problem, and the plan

The PCM buffer must lie in the game's device memory; our heap blocks and our own linear allocations are rejected. The plan:

1. Allocate our ring in linear memory with svcControlMemory op 0x10003. VA→PA is the same `+*(u32*)0x69ca4c` offset for the whole linear heap.
2. Hook 0x172be4 so it also accepts that range, then falls through to the original for anything else.

Otherwise, borrow from a game device-heap allocator (not yet found).
