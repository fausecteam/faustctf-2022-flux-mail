#!/usr/bin/env python3
import string
import random
import pwn
import time

#pwn.context.update(log_level="debug")

from ctf_gameserver import checkerlib

import utils


class TemplateChecker(checkerlib.BaseChecker):
    PORT = 4242
    DEBUG = False

    def _generate_random_string(self, size):
        return "".join(random.choice(string.ascii_letters) for x in range(size))

    def _retrieve(self, store, store_key):
        # Retrieve the given key from the specified store or generate.
        keys = checkerlib.load_state(store)
        if not keys:
            keys = {}
        if store_key in keys:
            return keys[store_key]
        value = self._generate_random_string(40)
        while value in keys.values():
            value = self._generate_random_string(40)
        keys[store_key] = value
        checkerlib.store_state(store, keys)
        return value

    def _revoke(self, store_name, key, value):
        store = checkerlib.load_state(store_name)
        if not store:
            return
        if (key in store) and (value == store[key]):
            store.pop(key)
            checkerlib.store_state(store_name, store)


    def _get_user_pass(self, flag):
        # Load, or generate if necessary, user information
        user = self._retrieve("users", flag)
        password = self._retrieve("passwords", user)
        return user, password

    def _revoke_user(self, user, password, flag):
        # If present, remove this user from the store
        self._revoke("passwords", user, password)
        self._revoke("users", flag, user)

    def _check_login_msg(self, msg):
        # Check existence of the 3 options and for login menu string
        if not b"Login menu:" in msg:
            return False
        if not b"(1) Create new user" in msg:
            return False
        if not b"(2) Login to existing user" in msg:
            return False
        if not b"(3) exit" in msg:
            return False
        return True

    def _check_auth_menu(self, msg):
        # Check existence of 4 options and for menu string
        if not b"Menu:" in msg:
            return False
        if not b"(1) View inbox" in msg:
            return False
        if not b"(2) View outbox" in msg:
            return False
        if not b"(3) Write new mail" in msg:
            return False
        if not b"(4) logout" in msg:
            return False
        return True

    def _create_user(self, user, password):
        # Create user and return connection
        try:
            if self.DEBUG:
                conn = pwn.process("./fluxmail")
            else:
                conn = pwn.remote(self.ip, self.PORT)
        except pwn.pwnlib.exception.PwnlibException:
            raise ConnectionRefusedError("cannot connect to target")
        msg = conn.recvuntil(b"(3) exit\n\t> ")
        if not self._check_login_msg(msg):
            pwn.log.info("_create_user: login message check failed")
            conn.close()
            return None, False, checkerlib.CheckResult.FAULTY
        conn.sendline(b"1")
        msg = conn.recvuntil(b"Username: ")
        conn.sendline(bytes(user, "ascii"))
        msg = conn.recvuntil(b"Password: ")
        conn.sendline(bytes(password, "ascii"))
        pwn.log.info("_create_user: submitted user information")

        msg = conn.recvline()
        if msg == b"\n":
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                return conn, False, None
            conn.close()
            return None, False, checkerlib.CheckResult.FAULTY
        if b"User is already in use!" in msg:
            conn.close()
            return None, True, None
        conn.close()
        return None, False, checkerlib.CheckResult.FAULTY

    def _login(self, user, password):
        # Login to the provided user and return connection
        try:
            if self.DEBUG:
                conn = pwn.process("./fluxmail")
            else:
                conn = pwn.remote(self.ip, self.PORT)
        except pwn.pwnlib.exception.PwnlibException:
            raise ConnectionRefusedError("cannot connect to target")
        msg = conn.recvuntil(b"exit\n\t> ")
        if not self._check_login_msg(msg):
            pwn.log.info("_login: login message check failed")
            conn.close()
            return None, False, checkerlib.CheckResult.FAULTY
        conn.sendline(b"2")
        msg = conn.recvuntil(b"Username: ")
        conn.sendline(bytes(user, "ascii"))
        msg = conn.recvuntil(b"Password: ")
        conn.sendline(bytes(password, "ascii"))
        pwn.log.info("_login: submitted user information")

        msg = conn.recvline()
        if msg == b"" or msg == b"\n": # TODO which one is it?
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                return conn, False, None
            conn.close()
            return None, False, checkerlib.CheckResult.FAULTY
        if b"Invalid credentials!" in msg:
            conn.close()
            return None, True, None
        conn.close()
        return None, False, checkerlib.CheckResult.FAULTY

    def _inbox(self, conn, entry=None):
        # Request inbox and search for an entry of provided
        conn.sendline(b"1")
        msg = conn.recvuntil(b"press ENTER!\n")
        conn.sendline(b"a")
        pwn.log.info("_inbox: sent nonempty confirmation!")
        msg = conn.recvline()

        if b"Sent data to server, waiting for" in msg:
            pwn.log.info("Teststring found in outbox")
            msg = conn.recvline()
        if b"There should be mail" in msg:
            pwn.log.info("Teststring found in inbox")
            msg = conn.recvline()

        if b"This mailbox is empty!" in msg:
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                return False, None
            conn.close()
            return False, checkerlib.CheckResult.FAULTY
        elif b"do you want to display?" in msg:
            conn.sendline(b"28") # definitely too much, so it will display as many mails as possible
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                found = False
                if bytes(entry, "ascii") is not None and bytes(entry, "ascii") in msg:
                    found = True
                return found, None
            conn.close()
            return False, checkerlib.CheckResult.FAULTY
        else:
            conn.close()
            return False, checkerlib.CheckResult.FAULTY

    def _outbox(self, conn, entry=None):
        # Request inbox and search for an entry of provided
        conn.sendline(b"2")
        msg = conn.recvuntil(b"press ENTER!\n")
        conn.sendline(b"a")
        msg = conn.recvline()

        if b"Sent data to server, waiting for" in msg:
            pwn.log.info("Teststring found in outbox")
            msg = conn.recvline()
        if b"There should be mail" in msg:
            pwn.log.info("Teststring found in outbox")
            msg = conn.recvline()

        if b"This mailbox is empty!" in msg:
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                return False, None
            conn.close()
            return False, checkerlib.CheckResult.FAULTY
        elif b"do you want to display?" in msg:
            conn.sendline(b"28") # definitely too much, so it will display as many mails as possible
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                found = False
                if bytes(entry, "ascii") is not None and bytes(entry, "ascii") in msg:
                    found = True
                return found, None
            conn.close()
            return False, checkerlib.CheckResult.FAULTY
        else:
            conn.close()
            return False, checkerlib.CheckResult.FAULTY

    def _send_mail(self, conn, to, text):
        # Send text to the user to
        conn.sendline(b"3")
        msg = conn.recvuntil(b"\nTo: ")
        conn.sendline(bytes(to, "ascii"))
        pwn.log.info("_send_mail: sending a message to: " + to)
        msg = conn.recvuntil(b"Message: ")
        conn.sendline(bytes(text, "ascii"))
        pwn.log.info("_send_mail: sending message: " + text)
        msg = conn.recvuntil(b"press ENTER!\n")
        conn.sendline(b"a")
        pwn.log.info("_send_mail: sent nonempty confirmation")
        msg = conn.recvline()
        if b"Mail sent!" in msg:
            msg = conn.recvuntil(b"logout\n\t> ")
            if self._check_auth_menu(msg):
                return None
            conn.close()
            return checkerlib.CheckResult.FAULTY
        conn.close()
        return checkerlib.CheckResult.FAULTY

    def _logout(self, conn):
        # Logout user from service
        conn.sendline(b"4")
        msg = conn.recvline()
        if b"Login menu" in msg:
            msg += conn.recvuntil(b"exit\n\t> ")
            if self._check_login_msg(msg):
                return None
            conn.close()
            return checkerlib.CheckResult.FAULTY
        conn.close()
        return checkerlib.CheckResult.FAULTY

    def _exit(self, conn):
        # Exit service
        conn.sendline(b"3")
        conn.close()
        return None

    def place_flag(self, tick):
        start = time.time()

        flag = checkerlib.get_flag(tick)
        user, pwd = self._get_user_pass(flag)

        conn, used, err = self._create_user(user, pwd)
        if err != None:
            pwn.log.info("place_flag: _create_user failed")
            pwn.log.info(f"Overall duration for place_flag: {int(time.time() - start)}s")
            return err

        while used == True:
            self._revoke_user(user, pwd, flag)
            user, pwd = self._get_user_pass(flag)
            conn, used, err = self._create_user(user, pwd)
            if err != None:
                pwn.log.info("place_flag: _create_user failed")
                pwn.log.info(f"Overall duration for place_flag: {int(time.time() - start)}s")
                return err

        pwn.log.info("place_flag: user created")

        err = self._send_mail(conn, "marty", flag)
        if err != None:
            pwn.log.info("place_flag: _send_mail failed")
            pwn.log.info(f"Overall duration for place_flag: {int(time.time() - start)}s")
            return err
        pwn.log.info("place_flag: mail sent")

        err = self._logout(conn)
        if err != None:
            pwn.log.info("place_flag: _logout failed")
            pwn.log.info(f"Overall duration for place_flag: {int(time.time() - start)}s")
            return err

        err = self._exit(conn)
        if err != None:
            pwn.log.info("place_flag: _exit failed")
            pwn.log.info(f"Overall duration for place_flag: {int(time.time() - start)}s")
            return err
        pwn.log.info("place_flag: placement finished")

        return checkerlib.CheckResult.OK

    def check_service(self):
        # Create user, send an email to itself and random user, check inbox and outbox
        start = time.time()
        user = self._generate_random_string(40)
        password = self._generate_random_string(40)

        conn, used, err = self._create_user(user, password)
        if err != None:
            pwn.log.info("check_service: _create_user failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err

        while used == True:
            user = self._generate_random_string(40)
            password = self._generate_random_string(40)
            conn, used, err = self._create_user(user, password)
            if err != None:
                pwn.log.info("check_service: _create_user failed")
                pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
                return err

        pwn.log.info("check_service: user created, start testing")
        mail_text = self._generate_random_string(50)
        err = self._send_mail(conn, user, mail_text)
        if err != None:
            pwn.log.info("check_service: _send_mail to self failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err
        pwn.log.info("check_service: mail sent to self")

        found, err = self._outbox(conn, entry=mail_text)
        if err != None:
            pwn.log.info("check_service: _outbox failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err
        if not found:
            conn.close()
            pwn.log.info("check_service: message not found in outbox")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return checkerlib.CheckResult.FAULTY
        pwn.log.info("check_service: mail retrieved from outbox")

        found, err = self._inbox(conn, entry=mail_text)
        if err != None:
            pwn.log.info("check_service: _inbox failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err
        if not found:
            conn.close()
            pwn.log.info("check_service: message not found in inbox")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return checkerlib.CheckResult.FAULTY
        pwn.log.info("check_service: mail retrieved from inbox")

        rand_user = self._generate_random_string(40)
        err = self._send_mail(conn, rand_user, mail_text)
        if err != None:
            pwn.log.info("check_service: _send_mail to random user failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err
        pwn.log.info("check_service: mail sent to random user")

        err = self._logout(conn)
        if err != None:
            pwn.log.info("check_service: _logout failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err

        err = self._exit(conn)
        if err != None:
            pwn.log.info("check_service: _exit failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return err
        pwn.log.info("check_service: ended connection")

        conn, nonexist, err = self._login(user, password)
        if err != None:
            pwn.log.info("check_service: _login failed")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return checkerlib.CheckResult.FLAG_NOT_FOUND
        if nonexist == True:
            pwn.log.info("check_service: user does not exist")
            pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
            return checkerlib.CheckResult.FAULTY
        pwn.log.info("check_service: logging in again works!")

        pwn.log.info(f"Overall duration for check_service: {int(time.time() - start)}s")
        return checkerlib.CheckResult.OK

    def check_flag(self, tick):
        flag = checkerlib.get_flag(tick)
        user, pwd = self._get_user_pass(flag)

        pwn.log.info(f"Checking flag {flag} for user {user}")

        conn, inval, err = self._login(user, pwd)
        if err != None:
            pwn.log.info("check_flag: _login failed")
            return checkerlib.CheckResult.FLAG_NOT_FOUND
        if inval == True:
            pwn.log.info("check_flag: user does not exist")
            return checkerlib.CheckResult.FLAG_NOT_FOUND

        pwn.log.info(f"check_flag: logged in, looking for flag now...")
        found, err = self._outbox(conn, entry=flag)
        if err != None:
            pwn.log.info("check_flag: _outbox failed")
            return err
        if not found:
            pwn.log.info("check_flag: flag not found in outbox")
            conn.close()
            return checkerlib.CheckResult.FLAG_NOT_FOUND
        pwn.log.info("check_flag: found flag!")

        err = self._logout(conn)
        if err != None:
            pwn.log.info("check_flag: _logout failed")
            conn.close()
            return err

        err = self._exit(conn)
        if err != None:
            pwn.log.info("check_flag: _exit failed")
            return err

        return checkerlib.CheckResult.OK

if __name__ == '__main__':

    checkerlib.run_check(TemplateChecker)
