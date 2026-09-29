import unittest

from collect_guest_cpu import parse, summarize


class CollectorTests(unittest.TestCase):
    def test_thread_names_and_reuse_identity(self):
        fields = ['0'] * 20
        fields[0], fields[11], fields[12], fields[19] = 'R', '12', '8', '1234'
        raw = 'PID 10\ncpu 100 0 30 200 20 0 0 0\ncpu0 0\ncpu1 0\n'
        raw += '11 (Worker (1)) ' + ' '.join(fields)
        result = parse(raw)
        self.assertEqual(result['cores'], 2)
        self.assertEqual(result['total'], 350)
        self.assertEqual(result['idle'], 220)
        self.assertEqual(result['threads'][('11', '1234')], ('Worker (1)', 20))

    def test_one_core_denominator(self):
        before = dict(total=100, idle=20, cores=6, pid='1',
                      threads={('2', 'start'): ('UnityMain', 10)})
        after = dict(total=700, idle=320, cores=6, pid='1',
                     threads={('2', 'start'): ('UnityMain', 110)})
        cpu, threads = summarize(before, after)
        self.assertIn('50% of 6 cores (3.0 cores busy)', cpu)
        self.assertIn('UnityMain 100%', threads)

    def test_restart_and_invalid_interval(self):
        before = dict(total=100, idle=20, cores=2, pid='1', threads={})
        after = dict(total=200, idle=60, cores=2, pid='2', threads={})
        self.assertIn('waiting for game', summarize(before, after)[1])
        self.assertIn('warming up', summarize(before, before)[0])


if __name__ == '__main__':
    unittest.main()
