# SPDX-FileCopyrightText: 2024 Google LLC
# SPDX-License-Identifier: Apache-2.0

from .. import PebbleCommander, exceptions, parsers


@PebbleCommander.command()
def app_list(cmdr):
    """ List applications.
    """
    return cmdr.send_prompt_command("app list")


@PebbleCommander.command()
def app_load_metadata(cmdr):
    """ Ghetto metadata loading for pbw_image.py
    """
    ret = cmdr.send_prompt_command("app load metadata")
    if not ret[0].startswith("OK"):
        raise exceptions.PromptResponseError(ret)


@PebbleCommander.command()
def app_launch(cmdr, idnum):
    """ Launch an application.
    """
    idnum = int(str(idnum), 0)
    if idnum == 0:
        raise exceptions.ParameterError('idnum out of range: %d' % idnum)
    ret = cmdr.send_prompt_command("app launch %d" % idnum)
    if not ret[0].startswith("OK"):
        raise exceptions.PromptResponseError(ret)


@PebbleCommander.command()
def app_remove(cmdr, idnum):
    """ Remove an application.
    """
    idnum = int(str(idnum), 0)
    if idnum == 0:
        raise exceptions.ParameterError('idnum out of range: %d' % idnum)
    ret = cmdr.send_prompt_command("app remove %d" % idnum)
    if not ret[0].startswith("OK"):
        raise exceptions.PromptResponseError(ret)


@PebbleCommander.command()
def app_resource_bank(cmdr, idnum=0):
    """ Get resource bank info for an application.
    """
    idnum = int(str(idnum), 0)
    if idnum < 0:
        raise exceptions.ParameterError('idnum out of range: %d' % idnum)
    ret = cmdr.send_prompt_command("resource bank info %d" % idnum)
    if not ret[0].startswith("OK "):
        raise exceptions.PromptResponseError(ret)
    return [ret[0][3:]]


@PebbleCommander.command()
def app_next_id(cmdr):
    """ Get next free application ID.
    """
    return cmdr.send_prompt_command("app next id")


@PebbleCommander.command()
def app_available(cmdr, idnum):
    """ Check if an application is available.
    """
    idnum = int(str(idnum), 0)
    if idnum == 0:
        raise exceptions.ParameterError('idnum out of range: %d' % idnum)
    return cmdr.send_prompt_command("app available %d" % idnum)


@PebbleCommander.command()
def app_status(cmdr, idnum):
    """ Get the status of an application (RUNNING, INSTALLED, or NOT_INSTALLED).

    Returns status information about the app:
    - RUNNING <type> <name>: App is currently running (type is 'app' or 'worker')
    - INSTALLED <name>: App is installed but not running
    - NOT_INSTALLED: App is not installed
    """
    idnum = int(str(idnum), 0)
    if idnum == 0:
        raise exceptions.ParameterError('idnum out of range: %d' % idnum)
    return cmdr.send_prompt_command("app status %d" % idnum)


@PebbleCommander.command()
def app_clear_db(cmdr):
    """ Clear all 3rd party apps from the database and cache.

    This is useful for ensuring a fresh state before reinstalling apps
    in the emulator without having to restart QEMU. Use this before
    reinstalling an app to ensure no cached state from previous versions
    persists.

    Usage:
        app-clear-db
        # Then reinstall your app
    """
    ret = cmdr.send_prompt_command("app clear db")
    if not ret[0].startswith("OK"):
        raise exceptions.PromptResponseError(ret)
