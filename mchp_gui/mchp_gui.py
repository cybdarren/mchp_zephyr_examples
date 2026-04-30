#!/usr/bin/env python3
"""
MCHP GUI — live sensor display for Microchip Zephyr applications.

Auto-detects the application type (IMU, HRM, Range, Weather) from the
$MCHP serial protocol and shows numeric values plus a scrolling chart.
"""

import tkinter as tk
from tkinter import ttk
import queue

from serial_parser import SerialReader, list_serial_ports
from app_views import VIEW_MAP

POLL_MS = 50


class MchpGui:
    def __init__(self, root):
        self.root = root
        self.root.title("MCHP Sensor GUI")
        self.root.geometry("700x500")
        self.root.minsize(500, 400)

        self.reader = None
        self.view = None
        self.detected_tag = None

        self._build_toolbar()
        self._build_status()

        self.view_frame = tk.Frame(self.root)
        self.view_frame.pack(fill=tk.BOTH, expand=True)

        self._show_placeholder("Select a COM port and click Connect")

    def _build_toolbar(self):
        bar = tk.Frame(self.root)
        bar.pack(fill=tk.X, padx=5, pady=5)

        tk.Label(bar, text="Port:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            bar, textvariable=self.port_var, width=12, state="readonly"
        )
        self.port_combo.pack(side=tk.LEFT, padx=4)

        self.refresh_btn = tk.Button(bar, text="Refresh", command=self._refresh_ports)
        self.refresh_btn.pack(side=tk.LEFT, padx=2)

        self.connect_btn = tk.Button(bar, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side=tk.LEFT, padx=4)

        self._refresh_ports()

    def _build_status(self):
        self.status_var = tk.StringVar(value="Disconnected")
        status = tk.Label(
            self.root,
            textvariable=self.status_var,
            anchor="w",
            relief=tk.SUNKEN,
            padx=5,
        )
        status.pack(fill=tk.X, side=tk.BOTTOM)

    def _refresh_ports(self):
        ports = list_serial_ports()
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def _toggle_connect(self):
        if self.reader:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_var.get()
        if not port:
            return

        try:
            self.reader = SerialReader(port)
            self.reader.start()
        except Exception as e:
            self.status_var.set(f"Error: {e}")
            self.reader = None
            return

        self.connect_btn.config(text="Disconnect")
        self.port_combo.config(state="disabled")
        self.refresh_btn.config(state="disabled")
        self.detected_tag = None
        self._clear_view()
        self._show_placeholder("Waiting for data...")
        self.status_var.set(f"Connected to {port}")
        self._poll()

    def _disconnect(self):
        if self.reader:
            self.reader.stop()
            self.reader = None

        self.connect_btn.config(text="Connect")
        self.port_combo.config(state="readonly")
        self.refresh_btn.config(state="normal")
        self.status_var.set("Disconnected")
        self._clear_view()
        self._show_placeholder("Select a COM port and click Connect")

    def _clear_view(self):
        if self.view:
            self.view.destroy()
            self.view = None
        for w in self.view_frame.winfo_children():
            w.destroy()
        self.detected_tag = None

    def _show_placeholder(self, text):
        tk.Label(
            self.view_frame, text=text, font=("Consolas", 14), fg="gray"
        ).pack(expand=True)

    def _poll(self):
        if not self.reader:
            return

        batch = 0
        while batch < 20:
            try:
                tag, values = self.reader.queue.get_nowait()
            except queue.Empty:
                break
            batch += 1

            if self.detected_tag is None and tag in VIEW_MAP:
                self.detected_tag = tag
                for w in self.view_frame.winfo_children():
                    w.destroy()
                app_name, view_cls = VIEW_MAP[tag]
                self.view = view_cls(self.view_frame)
                self.status_var.set(
                    f"Connected to {self.port_var.get()} — {app_name}"
                )

            if tag == self.detected_tag and self.view:
                self.view.update(values)

        self.root.after(POLL_MS, self._poll)


def main():
    root = tk.Tk()
    MchpGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
