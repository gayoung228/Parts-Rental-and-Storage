import unittest
from iot_camera_client import CameraConnection, Debouncer, Session, load_labels
from pathlib import Path


class CameraTests(unittest.TestCase):
    def test_labels_and_background(self):
        self.assertEqual(load_labels(Path(__file__).with_name('labels.txt')), ['1', '2', '3', '4', '5', 'NULL'])

    def test_stability_background_and_cooldown(self):
        detector = Debouncer()
        self.assertIsNone(detector.update('64', .95, 0))
        self.assertIsNone(detector.update('4', .95, .1))
        self.assertIsNone(detector.update('64', .95, .2))
        self.assertIsNone(detector.update('64', .95, .3))
        self.assertEqual(detector.update('64', .95, .4), '64')
        detector.sent(.4)
        self.assertIsNone(detector.update('64', .99, 10))
        detector.update('NULL', .99, 10.1)
        detector.update('NULL', .99, 10.2)
        detector.update('NULL', .99, 10.3)
        detector.update('4', .95, 10.4)
        detector.update('4', .95, 10.5)
        self.assertEqual(detector.update('4', .95, 10.6), '4')
        detector.sent(10.6)
        for t in (10.7, 10.8, 10.9):
            detector.update('NULL', .95, t)
        for t in (11, 11.1, 11.2):
            self.assertIsNone(detector.update('1', .95, t))
        self.assertEqual(detector.update('1', .95, 12.7), '1')
        detector.reset()
        for _ in range(4):
            self.assertIsNone(detector.update('64', float('nan'), 20))

    def test_session_ownership_and_receipts(self):
        connection = CameraConnection('127.0.0.1', 5000, 'LOCKER_101')
        session = Session('E6044006', '0000000100000001')
        self.assertFalse(connection.submit(session, '64', .95))
        connection.receive_line('[SERVER]SESSION@LOCKER_101@E6044006@0000000100000001@OPEN')
        self.assertEqual(connection.current_session(), session)
        self.assertFalse(connection.submit(session, '64', .5))
        self.assertFalse(connection.submit(session, 'NULL', .95))
        self.assertTrue(connection.submit(session, '64', .95))
        event = connection.pending['event']
        self.assertIn(f'DETECT@{session.identifier}@64@950@{event}', connection.pending['line'])
        connection.receive_line('[SERVER]DETECT@0000000000000000@SAVED')
        self.assertIsNotNone(connection.pending)
        connection.receive_line(f'[SERVER]DETECT@{event}@SAVED')
        self.assertIsNone(connection.pending)
        self.assertTrue(connection.submit(session, '4', .95))
        connection.receive_line('[SERVER]SESSION@LOCKER_101@D3693D06@0000000100000002@OPEN')
        self.assertIsNone(connection.pending)
        self.assertFalse(connection.submit(session, '64', .95))
        connection.receive_line('[SERVER]SESSION@LOCKER_101@NONE@NONE@CLOSED')
        self.assertIsNone(connection.current_session())

    def test_rental_receipts(self):
        connection = CameraConnection('127.0.0.1', 5000, 'LOCKER_101')
        connection.receive_line('[SERVER]SESSION@LOCKER_101@E6044006@0000000100000001@OPEN')
        for status in ('RENTED', 'RETURNED', 'BUSY', 'UNCHANGED'):
            self.assertTrue(connection.submit(connection.current_session(), '1', .95))
            event = connection.pending['event']
            connection.receive_line(f'[SERVER]DETECT@{event}@{status}')
            self.assertIsNone(connection.pending)


if __name__ == '__main__':
    unittest.main()
