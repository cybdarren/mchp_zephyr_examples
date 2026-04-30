#!/usr/bin/env python3
"""
MCHP GUI — live sensor display for Microchip Zephyr applications.

Displays up to 4 devices simultaneously in a 2x2 grid. Each panel
auto-detects the application type (IMU, HRM, Range, Weather) from the
$MCHP serial protocol and shows numeric values plus a scrolling chart.
"""

import tkinter as tk
from tkinter import ttk
import queue

from serial_parser import SerialReader, list_serial_ports
from app_views import VIEW_MAP

POLL_MS = 20
NUM_PANELS = 4


class DevicePanel:
    def __init__(self, parent, panel_id):
        self.panel_id = panel_id
        self.reader = None
        self.view = None
        self.detected_tag = None

        self.frame = tk.LabelFrame(parent, text=f"Device {panel_id + 1}", padx=4, pady=4)

        self._build_toolbar()

        self.view_frame = tk.Frame(self.frame)
        self.view_frame.pack(fill=tk.BOTH, expand=True)

        self.status_var = tk.StringVar(value="Disconnected")
        tk.Label(
            self.frame,
            textvariable=self.status_var,
            anchor="w",
            relief=tk.SUNKEN,
            font=("Consolas", 9),
            padx=3,
        ).pack(fill=tk.X, side=tk.BOTTOM)

        self._show_placeholder("Not connected")

    def _build_toolbar(self):
        bar = tk.Frame(self.frame)
        bar.pack(fill=tk.X, pady=(0, 4))

        tk.Label(bar, text="Port:", font=("Consolas", 9)).pack(side=tk.LEFT)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            bar, textvariable=self.port_var, width=10, state="readonly",
            font=("Consolas", 9)
        )
        self.port_combo.pack(side=tk.LEFT, padx=2)

        self.refresh_btn = tk.Button(
            bar, text="Refresh", command=self._refresh_ports, font=("Consolas", 8)
        )
        self.refresh_btn.pack(side=tk.LEFT, padx=1)

        self.connect_btn = tk.Button(
            bar, text="Connect", command=self._toggle_connect, font=("Consolas", 8)
        )
        self.connect_btn.pack(side=tk.LEFT, padx=2)

        self._refresh_ports()

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

    def _disconnect(self):
        if self.reader:
            self.reader.stop()
            self.reader = None

        self.connect_btn.config(text="Connect")
        self.port_combo.config(state="readonly")
        self.refresh_btn.config(state="normal")
        self.status_var.set("Disconnected")
        self._clear_view()
        self._show_placeholder("Not connected")

    def _clear_view(self):
        if self.view:
            self.view.destroy()
            self.view = None
        for w in self.view_frame.winfo_children():
            w.destroy()
        self.detected_tag = None

    def _show_placeholder(self, text):
        tk.Label(
            self.view_frame, text=text, font=("Consolas", 11), fg="gray"
        ).pack(expand=True)

    def poll(self):
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
                self.frame.config(text=f"Device {self.panel_id + 1} — {app_name}")
                self.status_var.set(
                    f"{self.port_var.get()} — {app_name}"
                )

            if tag == self.detected_tag and self.view:
                self.view.update(values)


class MchpGui:
    def __init__(self, root):
        self.root = root
        self.root.title("MCHP Sensor GUI")
        self.root.geometry("1200x800")
        self.root.minsize(800, 600)

        self.root.columnconfigure(0, weight=1)
        self.root.columnconfigure(1, weight=1)
        self.root.rowconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        self.panels = []
        for i in range(NUM_PANELS):
            row, col = divmod(i, 2)
            panel = DevicePanel(self.root, i)
            panel.frame.grid(row=row, column=col, sticky="nsew", padx=4, pady=4)
            self.panels.append(panel)

        self._poll()

    def _poll(self):
        for panel in self.panels:
            panel.poll()
        self.root.after(POLL_MS, self._poll)


def main():
    root = tk.Tk()
    MchpGui(root)
    root.mainloop()


if __name__ == "__main__":
    main()
