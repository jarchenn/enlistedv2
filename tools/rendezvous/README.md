# Rendezvous + relay server for internet play

`rendezvous.py` is a small UDP server: one Python file, standard library only. It lets players on different networks find each other by **room code**:

1. The host registers a room and gets a code such as `K7Q2MX`.
2. A friend types the code.
3. The server introduces the two so they can connect **directly** (UDP hole punching).
4. When that fails, for example on campus Wi-Fi or a symmetric NAT, the server **relays** the game traffic.

The server stores no game data: only rooms, addresses and relay pairs, in memory.

| File | What it is |
|---|---|
| `rendezvous.py` | The server. `--selftest` runs its tests; `--probe HOST:PORT` checks a running server. |
| `rendezvous.service` | systemd unit (runs it on boot, restarts it if it crashes). |
| `rendezvous.conf.example` | Optional settings (shared secret, port). |
| `PROTOCOL.md` | Exact wire format (for the game's C++ client). |

Default port: **UDP 27900**.

The guide below puts it on an **Oracle Cloud Always Free** VM. It costs $0 and you don't need any cloud experience. Plan on about 30–45 minutes, most of it waiting for Oracle's sign-up and the VM to start.

> Oracle's console moves buttons around now and then. If a name below doesn't match exactly, look for the closest one; the steps stay the same.

---

## 1. Create the Oracle Cloud account

1. Go to <https://www.oracle.com/cloud/free/> and click **Start for free**.
2. Fill in the form.
   - Oracle asks for a **credit or debit card to verify your identity**. Always Free resources are not charged.
   - Stay on the free tier: don't click "Upgrade to paid" anywhere.
3. **Home region:** pick one close to where you and your friends play, for example *US East (Ashburn)* or *Germany Central (Frankfurt)*.
   - You **can't change it later**.
   - Always Free VMs only run in the home region.
4. Wait for the "your account is ready" email (a few minutes, sometimes longer), then sign in at <https://cloud.oracle.com>.

## 2. Create the VM (Always Free, Ubuntu)

1. Click the ☰ menu (top left), then **Compute → Instances → Create instance**.
2. **Name:** `nmk-rendezvous`.
3. **Image and shape → Edit:**
   - **Image:** **Change image → Ubuntu**, then pick **Canonical Ubuntu 24.04** (22.04 is fine too). Don't pick the "Minimal" image.
   - **Shape:** **Change shape**, then either:
     - **AMD → VM.Standard.E2.1.Micro** (it has the "Always Free eligible" label), or
     - **Ampere → VM.Standard.A1.Flex** with 1 OCPU and 6 GB memory (also Always Free).

     Either works; the server needs almost nothing. If you get an "out of capacity" error, try the other shape or another availability domain.
4. **Networking:** leave **Create new virtual cloud network** and **Create new public subnet** selected, and make sure **Assign a public IPv4 address** is on.
5. **Add SSH keys:**
   - Choose **Generate a key pair for me**.
   - Click **Save private key** (and **Save public key**).
   - Keep the `.key` file somewhere safe, for example `C:\Users\<you>\.ssh\oracle-nmk.key`. **You can't download it again.**
6. Click **Create**.
7. Wait until the state is **Running**, then copy the **Public IP address** from the instance page, for example `203.0.113.7`. This guide calls it `<IP>` from here on.

## 3. Open UDP 27900 in the VCN security list (Oracle's cloud firewall)

1. On the instance page, under **Primary VNIC** (or the **Networking** tab), click the **Subnet** link.
2. Open the subnet's **Security Lists** (or the **Security** tab), then the **Default Security List for …**.
3. Click **Add Ingress Rules** and fill in:
   - **Source Type:** CIDR
   - **Source CIDR:** `0.0.0.0/0`
   - **IP Protocol:** **UDP**
   - **Source Port Range:** leave empty (All)
   - **Destination Port Range:** `27900`
   - **Description:** `Normandy Mapmaker rendezvous`
4. Click **Add Ingress Rules**.

Leave the existing rule for TCP 22 (SSH) alone.

## 4. Connect to the VM over SSH

On Windows, open **PowerShell**. `ssh` is built into Windows 10 and 11.

1. Tighten the key file's permissions. If you skip this, ssh refuses a key that other users can read.

   ```powershell
   icacls "$env:USERPROFILE\.ssh\oracle-nmk.key" /inheritance:r /grant:r "$($env:USERNAME):R"
   ```

2. Connect:

   ```powershell
   ssh -i "$env:USERPROFILE\.ssh\oracle-nmk.key" ubuntu@<IP>
   ```

   - Answer `yes` to the fingerprint question.
   - The user name is `ubuntu` on Ubuntu images.
   - You're in when the prompt looks like `ubuntu@nmk-rendezvous:~$`.

On macOS or Linux, run `chmod 600 oracle-nmk.key` first, then the same `ssh -i oracle-nmk.key ubuntu@<IP>`.

## 5. Open UDP 27900 in the VM's own firewall

Oracle's Ubuntu images ship with **iptables rules that reject everything except SSH**, even after step 3. Both firewalls must allow the port.

On the VM, run:

```bash
sudo iptables -I INPUT -p udp --dport 27900 -j ACCEPT
sudo netfilter-persistent save
sudo iptables -L INPUT -n --line-numbers | head
```

The last command should show the `ACCEPT udp ... dpt:27900` rule near the top, above the final `REJECT` rule. `netfilter-persistent save` keeps the rule after reboots.

If the image uses `ufw` instead (`sudo ufw status` says *active*), run `sudo ufw allow 27900/udp`.

## 6. Copy the server to the VM and start it

1. **On your PC**, in PowerShell, from the repository folder:

   ```powershell
   scp -i "$env:USERPROFILE\.ssh\oracle-nmk.key" tools\rendezvous\rendezvous.py tools\rendezvous\rendezvous.service tools\rendezvous\rendezvous.conf.example ubuntu@<IP>:~
   ```

2. **On the VM**, over SSH:

   ```bash
   sudo mkdir -p /opt/nmk-rendezvous
   sudo cp ~/rendezvous.py /opt/nmk-rendezvous/
   sudo cp ~/rendezvous.conf.example /opt/nmk-rendezvous/rendezvous.conf
   sudo cp ~/rendezvous.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now rendezvous
   systemctl status rendezvous --no-pager
   ```

   `status` should say **active (running)**, followed by a line like `rendezvous: listening on udp 0.0.0.0:27900 (secret off, asyncio)`.

### Optional: shared secret

A shared secret means only builds that know it can **host** rooms. Joining by code never needs the secret.

```bash
sudo nano /opt/nmk-rendezvous/rendezvous.conf      # set:  secret = pick-a-long-random-phrase
sudo systemctl restart rendezvous
```

In nano, save with Ctrl+O, Enter, and quit with Ctrl+X.

This isn't strong security: the secret travels unencrypted. It keeps strangers' copies of the game from using your server.

## 7. Verify

1. **Self-test on the VM.** It runs on 127.0.0.1 on a random port, so it doesn't disturb the live service:

   ```bash
   python3 -I /opt/nmk-rendezvous/rendezvous.py --selftest
   ```

   The last line must be `rendezvous selftest: N/N passed`.

2. **Probe from outside.** This checks both firewalls end to end. Run it from your PC or any machine **outside** Oracle.

   With netcat (Linux, macOS or WSL):

   ```bash
   printf 'NMKRK{"code":"TEST22","ver":0}' | nc -u -w2 <IP> 27900
   ```

   Expected output: `NMKRN{"code":"TEST22"}` ("no such room"). No output means a firewall is still closed: recheck steps 3 and 5.

   Windows has no `nc`, but any Python 3 can run the probe:

   ```powershell
   python -I tools\rendezvous\rendezvous.py --probe <IP>:27900
   ```

   Expected output: `reply from <IP>:27900: b'NMKRN{"code":"TEST22"}'`.

3. **Logs.** Run `journalctl -u rendezvous -f` (Ctrl+C to stop). Every 5 minutes the server prints a line with the room count, relay count and traffic.

## 8. Optional: a free hostname with DuckDNS

An IP address works fine. A name is easier to remember, and if you ever rebuild the VM you only update DuckDNS, not every PC.

1. Go to <https://www.duckdns.org> and sign in (GitHub, Google, …).
2. Under **domains**, type a name, for example `nmk-alex`, and click **add domain**.
3. In the **current ip** box next to it, enter `<IP>` and click **update ip**.
4. Your server is now `nmk-alex.duckdns.org:27900`. Check it with:

   ```powershell
   python -I tools\rendezvous\rendezvous.py --probe nmk-alex.duckdns.org:27900
   ```

The VM's public IP stays the same while the instance exists. If you terminate the VM and create a new one, update the IP on DuckDNS.

## 9. Point the game at the server

On **every PC** that plays, create or edit `net_user.json` **in the game's exe folder** (next to `NormandyMapmaker.exe`, not in `content/`):

```json
{
  "server": "203.0.113.7:27900",
  "secret": ""
}
```

- **`server`:** `<IP>:27900` or `yourname.duckdns.org:27900`.
- **`secret`:** the same string as in `rendezvous.conf`, or `""` if you didn't set one.

The game also saves your last player name and room code in this file. The **Server** field in the lobby's **Advanced** section edits the same setting.

Then:
1. The host turns **Internet: on** in the lobby.
2. The host shares the **Room code**.
3. Friends enter it under **Join → Room code → Join room**.

---

## Updating the server

```powershell
scp -i "$env:USERPROFILE\.ssh\oracle-nmk.key" tools\rendezvous\rendezvous.py ubuntu@<IP>:~
```

```bash
sudo cp ~/rendezvous.py /opt/nmk-rendezvous/ && sudo systemctl restart rendezvous
```

The first command runs on your PC, the second on the VM. Rooms live in memory, so a restart forgets them; running hosts re-register automatically within 5 s and keep their codes.

## Troubleshooting

| Symptom | Check |
|---|---|
| Probe gets no reply | Both firewalls (steps 3 and 5). Check the service is running: `systemctl status rendezvous`. |
| Game says "Server unreachable" | The `server` value in `net_user.json`: `host:port`, no `udp://`. |
| Host shows a secret error | `secret` in `net_user.json` must match `rendezvous.conf` exactly. |
| Joining always says "using relay" | Normal on campus or public Wi-Fi and on some mobile networks. The relay adds one server hop of latency. |
| `ssh: Permissions for key are too open` | Run the `icacls` line in step 4. |

## Costs and limits

**Cost:** Always Free covers this VM and about **10 TB of outbound traffic a month**.
- Direct (punched) games use almost no server traffic.
- A relayed player uses roughly 20–60 KB/s each way.
- The server caps relays at 2 MB/s per room and 20 MB/s in total, so one busy weekend can't come anywhere near the allowance.

**Idle reclamation:** Oracle may reclaim Always Free VMs that stay almost completely idle for a week, and this server *is* almost idle. Oracle emails you before it does that. If it happens, create a new VM with this guide and update DuckDNS. Upgrading the account to Pay-As-You-Go stops reclamation but needs care to stay within the free limits; it isn't needed to get started.

**Network:** the server is IPv4 only.

## Running locally (development)

```bash
python -I tools/rendezvous/rendezvous.py --port 27900          # server on this PC
python -I tools/rendezvous/rendezvous.py --selftest            # tests (127.0.0.1, random port)
```

If a Python build has no working `asyncio`, as with the stripped embeddable Python in `%TEMP%/nmk/py`, the server falls back to a plain socket loop with the same behaviour. `--plain` forces that loop.
