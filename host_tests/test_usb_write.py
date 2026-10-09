"""Compile the actual locked USB writer against an IDF-style atomic TX sink."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class USBWriteTests(unittest.TestCase):
    def test_atomic_ring_long_reply_and_failures(self):
        source = (ROOT / 'platform/espidf/runtime.c').read_text(encoding='utf8')
        start = source.index('void esp_agent_write(')
        end = source.index('\nstatic void say(', start)
        body = source[start:end]
        found = re.search(r'enum \{ USB_TX_BYTES = (\d+) \};', source)
        # The original failed implementation predates the shared constant.
        capacity = int(found.group(1)) if found else 2048
        program = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static int lock_storage;
static void *output_lock = &lock_storage;
static unsigned taken, released, attempts;
static int allow_lock = 1, locked, stop_call, negative_stop, partial;
static unsigned char received[16384];
static size_t received_bytes;
static int xSemaphoreTake(void *lock, unsigned timeout)
{
    assert(lock == output_lock && timeout == 2000 && !locked);
    ++taken;
    if (!allow_lock) return 0;
    locked = 1;
    return 1;
}
static void xSemaphoreGive(void *lock)
{
    assert(lock == output_lock && locked);
    locked = 0;
    ++released;
}
static int usb_serial_jtag_write_bytes(const void *bytes, size_t size, unsigned timeout)
{
    assert(locked && timeout == 100);
    ++attempts;
    if (size > USB_TX_BYTES || (stop_call && attempts == (unsigned)stop_call))
        return negative_stop ? -1 : 0;
    if (partial && size > 73) size = 73;
    assert(received_bytes + size <= sizeof(received));
    memcpy(received + received_bytes, bytes, size);
    received_bytes += size;
    return (int)size;
}
'''
        program = program.replace('#include <string.h>', '#include <string.h>\nenum { USB_TX_BYTES = ' + str(capacity) + ' };')
        program += body
        program += r'''
static void reset(void)
{
    taken = released = attempts = 0;
    allow_lock = 1; locked = stop_call = negative_stop = partial = 0;
    received_bytes = 0;
    output_lock = &lock_storage;
}
int main(void)
{
    char input[16384];
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = (char)(i * 17 + 3);
    const size_t lengths[] = {0, 1, 1536, 2048, 2049, 2356, 2484, 4096, 16384};
    for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
        reset();
        esp_agent_write(input, lengths[i]);
        if (received_bytes != lengths[i]) {
            fprintf(stderr, "atomic TX lost reply: expected%zu, got%zu\n", lengths[i], received_bytes);
            return 2;
        }
        assert(!memcmp(input, received, received_bytes));
        assert(taken == 1 && released == 1 && !locked);
        if (lengths[i] <= USB_TX_BYTES) assert(attempts == (lengths[i] ? 1u : 0u));
    }
    reset(); partial = 1;
    esp_agent_write(input, sizeof(input));
    assert(received_bytes == sizeof(input) && !memcmp(input, received, received_bytes));
    assert(released == 1 && !locked);
    for (negative_stop = 0; negative_stop < 2; ) {
        int negative = negative_stop;
        reset(); stop_call = 2; negative_stop = negative;
        esp_agent_write(input, sizeof(input));
        assert(attempts == 2 && received_bytes == USB_TX_BYTES && released == 1 && !locked);
        negative_stop = negative + 1;
    }
    reset(); allow_lock = 0;
    esp_agent_write(input, sizeof(input));
    assert(taken == 1 && !released && !attempts);
    reset(); output_lock = NULL;
    esp_agent_write(input, sizeof(input));
    assert(!taken && !released && !attempts);
    puts("actual USB writer:9 lengths, partial writes,0/-1 timeout and lock failure passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / 'usb.c').write_text(program, encoding='utf8')
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                            str(work / 'usb.c'), '-o', str(work / 'usb')], check=True)
            subprocess.run([str(work / 'usb')], check=True)
        if found:
            self.assertIn('.tx_buffer_size = USB_TX_BYTES', source)


if __name__ == '__main__':
    unittest.main()
