import os
import argparse
from matplotlib.ticker import FuncFormatter
import matplotlib.pyplot as plt

mhz=pow(2, 20)
default_cycles = 16*2800.200*mhz

mem="mem"
cpu_total="cpu_total"
tx_bytes="tx_bytes"
rx_bytes="rx_bytes"
tx_packets="tx_packets"
rx_packets="rx_packets"

def parse():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", "-d", help="path of results directory")
    parser.add_argument("--exclude-systems", "-ex", nargs='*', help="exclude systems")
    parser.add_argument("--normalize", "-n", help="name of system to normalize by")
    parser.add_argument("--cycles", "-c", type=float, help="cycles in mhz default is: {2800.200}")
    return parser.parse_args()

def parse_stats(result_str, test_results):
    split = result_str.split("\n")
    for s in split:
        if cpu_total in s:
            test_results[cpu_total].append(float(s.replace(f"{cpu_total}:","").strip()))
        if mem in s:
            test_results[mem].append(float(s.replace(f"{mem}:","").strip()))
        if tx_bytes in s:
            test_results[tx_bytes].append(float(s.replace(f"{tx_bytes}:","").strip()))
        if rx_bytes in s:
            test_results[rx_bytes].append(float(s.replace(f"{rx_bytes}:","").strip()))
        if tx_packets in s:
            test_results[tx_packets].append(float(s.replace(f"{tx_packets}:","").strip()))
        if rx_packets in s:
            test_results[rx_packets].append(float(s.replace(f"{rx_packets}:","").strip()))

# result file name: <test>_<system>_collect_<num>
def parse_results(results_dir=".", exclude=None, normalize_by=None, cycles=default_cycles):
    test_results = {}
    for result in os.listdir(results_dir):
        split = result.rsplit("_", 3)
        if len(split) < 4 and split[:-2] != "collect":
            print(f"bad result file: {result}")
            continue

        test=split[0]
        system=split[1]

        # print(f"test: {split[0]}, system: {split[1]}, run:{split[3]}")

        if test not in test_results:
            test_results[test] = {}

        if system not in test_results[test]:
            if exclude is not None and system in exclude:
                print(f"system: {system} is in exclude list: {exclude}")
                continue
            test_results[test][system] = {cpu_total:[], mem:[], tx_bytes: [], tx_packets: [], rx_bytes: [], rx_packets: [] }

        try:
            with open(os.path.join(results_dir, result), "r") as f:
                parse_stats(f.read(), test_results[test][system])     
                
        
        except Exception as ex:
            print(f"failed to parse: {result}, ex: {ex}")
    
   # print(test_results)
    graph_results = {}
    for t, s in test_results.items():
        graph_results[t] = {}
        systems = []
        vmems = []
        vbytes = []
        vpackets = []
        vbytes_pack = []
        tbytes = []
        tpackets = []
        vcpu = []
        for ks, vs in s.items():
            total_cpu = (sum(vs[cpu_total])/len(vs[cpu_total]))
            cpu = (sum(vs[cpu_total])/len(vs[cpu_total]))*cycles
            avg_bytes = ((sum(vs[tx_bytes]) + sum(vs[rx_bytes])) / len(vs[tx_bytes]))
            xbytes = cpu / avg_bytes
            avg_packets = ((sum(vs[tx_packets]) + sum(vs[rx_packets])) / len(vs[tx_packets]))
            xpackets = cpu / avg_packets
            bytes_pack = avg_bytes/avg_packets

            avg_bytes = avg_bytes/(1024.0 * 1024.0 * 256.0)
            avg_packets = avg_packets/(1000 * 1000)
            xmem = sum(vs[mem]) / len(vs[mem])
            xmem = xmem/(1024.0 * 1024)

            graph_results[t][ks] = [xbytes, xpackets, xmem, avg_bytes, avg_packets, bytes_pack, total_cpu]
            systems.append(ks)
            vmems.append(xmem)
            vbytes.append(xbytes)
            vpackets.append(xpackets)
            vbytes_pack.append(xpackets)
            tbytes.append(avg_bytes)
            tpackets.append(avg_packets)
            vcpu.append(total_cpu)


            print(f"system:{ks}, averages bytes={avg_bytes:.2f}, packets={avg_packets:.2f}, mem={xmem:.2f}")

        plt.rcParams.update({'font.size': 16})
        plt.grid(axis='y')
        plt.bar(systems, vcpu)
        plt.title("Cpu Util %")
        plt.savefig(f"{t}-cpu.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, vbytes_pack)
        plt.title("Bytes/Packet")
        plt.savefig(f"{t}-bytes-packet.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, vmems)
        plt.title("Memory GB.")
        plt.savefig(f"{t}-memory.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, vbytes)
        plt.xticks(rotation=25)
        plt.title("Cycles / Bytes.")
        plt.savefig(f"{t}-cycles-bytes.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, vpackets)
        plt.title("Cycles / Packets.")
        plt.savefig(f"{t}-cycles-packets.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, tbytes)
        plt.title("Gb/s.")
        plt.savefig(f"{t}-bytes.pdf")
        plt.show()
        plt.clf()
        plt.grid(axis='y')
        plt.bar(systems, tpackets)
        plt.title("Mpps")
        plt.savefig(f"{t}-packets.pdf")
        plt.show()
        plt.clf()

        if normalize_by is not None:
            if normalize_by not in s.keys():
                print(f"cant normalize by: {normalize_by} possible values: {s.keys()}")
            else:
                nbytes = graph_results[t][normalize_by][3]
                npackets = graph_results[t][normalize_by][4]
                plt.bar(systems, [x/nbytes for x in tbytes])
                plt.grid(axis='y')
                plt.title(f"Bytes normalized by {normalize_by}.")
                plt.savefig(f"{t}-bytes-normalized-by-{normalize_by}.pdf")
                plt.show()
                plt.clf()
                plt.grid(axis='y')
                plt.bar(systems, [x/npackets for x in tpackets])
                plt.title(f"Packets normalized by {normalize_by}.")
                plt.savefig(f"{t}-packets-normalized-by-{normalize_by}.pdf")
                plt.show()
                plt.clf()


if __name__ == "__main__" :
    args = parse()
    results_dir = args.results_dir or "results"
    cycles = args.cycles or default_cycles
    parse_results(results_dir=results_dir, exclude=args.exclude_systems, normalize_by=args.normalize, cycles=cycles)
