#!/usr/bin/env python3
import json
import os
import sys
import xml.etree.ElementTree as ET
from xml.dom import minidom

def generate_iodd(config):
    # Namespace and basic structure
    ns = {
        "": "http://www.io-link.com/IODD/2010/11",
        "xsi": "http://www.w3.org/2001/XMLSchema-instance"
    }
    for prefix, uri in ns.items():
        ET.register_namespace(prefix, uri)

    root = ET.Element("IODevice", {
        "xmlns": ns[""],
        "xmlns:xsi": ns["xsi"],
        "xsi:schemaLocation": "http://www.io-link.com/IODD/2010/11 IODD1.1.xsd"
    })

    # DocumentInfo
    doc_info = ET.SubElement(root, "DocumentInfo", {
        "copyright": "iolinki-project",
        "releaseDate": "2026-02-01",
        "version": "V1.1"
    })

    # ProfileHeader
    profile_header = ET.SubElement(root, "ProfileHeader")
    ET.SubElement(profile_header, "ProfileIdentification").text = "IO-Link Device Profile"
    ET.SubElement(profile_header, "ProfileRevision").text = "1.1"

    # ProfileBody
    profile_body = ET.SubElement(root, "ProfileBody")
    
    # DeviceIdentity
    dev_ident = ET.SubElement(profile_body, "DeviceIdentity", {
        "vendorId": str(config.get("vendorId", 0)),
        "vendorName": config.get("vendorName", "Unknown"),
        "deviceId": str(config.get("deviceId", 0))
    })
    
    # DeviceFunction
    dev_func = ET.SubElement(profile_body, "DeviceFunction")

    # VariableCollection (ISDU Parameters)
    var_coll = ET.SubElement(dev_func, "VariableCollection")
    for var in config.get("variables", []):
        v = ET.SubElement(var_coll, "Variable", {
            "index": str(var["index"]),
            "accessRights": var.get("accessRights", "rw"),
            "id": var["id"]
        })
        ET.SubElement(v, "Datatype", {"xsi:type": "IntegerT", "bitLength": str(var.get("bitLength", 8))})
        name = ET.SubElement(v, "Name")
        ET.SubElement(name, "PrimaryLanguage").text = var["name"]

    # ProcessDataCollection
    pd_coll = ET.SubElement(dev_func, "ProcessDataCollection")
    for pd in config.get("processData", []):
        pd_item = ET.SubElement(pd_coll, "ProcessData", {"id": pd["id"]})
        if "in" in pd:
            pd_in = ET.SubElement(pd_item, "ProcessDataIn", {"id": pd["in"]["id"], "bitLength": str(pd["in"]["bitLength"])})
            ET.SubElement(pd_in, "Datatype", {"xsi:type": "RecordT", "bitLength": str(pd["in"]["bitLength"])})
        if "out" in pd:
            pd_out = ET.SubElement(pd_item, "ProcessDataOut", {"id": pd["out"]["id"], "bitLength": str(pd["out"]["bitLength"])})
            ET.SubElement(pd_out, "Datatype", {"xsi:type": "RecordT", "bitLength": str(pd["out"]["bitLength"])})

    # ExternalResources (simplified)
    ext_res = ET.SubElement(root, "ExternalResources")
    lang_res = ET.SubElement(ext_res, "Language", {"xml:lang": "en-US"})

    # Writing to file
    xml_str = ET.tostring(root, encoding="utf-8")
    parsed_xml = minidom.parseString(xml_str)
    pretty_xml = parsed_xml.toprettyxml(indent="  ")

    output_file = config.get("output", "device.xml")
    with open(output_file, "w") as f:
        f.write(pretty_xml)
    print(f"IODD generated at {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: iodd_gen.py <config.json>")
        sys.exit(1)
    
    with open(sys.argv[1], "r") as f:
        config = json.load(f)
    
    generate_iodd(config)
