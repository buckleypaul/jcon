.. zephyr:code-sample:: hello_jcon
   :name: jcon hello sample

   Emit a small JSON document to the console using the jcon streaming emitter.

Overview
********

This sample shows the minimum wiring needed to emit JSON with jcon under
Zephyr: enable ``CONFIG_JCON=y``, supply a ``putc`` callback, call
``jcon_start`` / ``jcon_add_*`` / ``jcon_end``.

Building and Running
********************

From a west workspace that includes this repository as a module, build for
``native_sim`` and run:

.. code-block:: console

   west build -b native_sim samples/zephyr/hello_jcon
   west build -t run

If jcon is not yet a listed module in your ``west.yml``, add it on the fly
with ``ZEPHYR_EXTRA_MODULES``:

.. code-block:: console

   west build -b native_sim samples/zephyr/hello_jcon \
       -- -DZEPHYR_EXTRA_MODULES=/absolute/path/to/jcon
   west build -t run

Expected Output
***************

.. code-block:: text

   jcon hello sample
   {
     "board": "native_sim",
     "uptime_ms": 3,
     "features": [
       "streaming",
       "zero-heap"
     ]
   }
   jcon status: 0

``uptime_ms`` will vary run to run.
