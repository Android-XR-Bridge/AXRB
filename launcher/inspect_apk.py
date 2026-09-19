"""Read APK identity with Android SDK tools; never install or modify the APK."""
import argparse, base64, json, re, subprocess, zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

def _manifest_activity(output, package):
    """Find an enabled public MAIN entry point omitted by aapt's badging view."""
    root = ET.Element('root')
    stack = [(-1, root)]
    for line in output.splitlines():
        element = re.match(r'(\s*)E: ([\w-]+)', line)
        if element:
            depth = len(element[1])
            while stack[-1][0] >= depth:
                stack.pop()
            stack.append((depth, ET.SubElement(stack[-1][1], element[2])))
        else:
            attribute = re.match(r'\s*A: http://schemas.android.com/apk/res/android:'
                r'(name|enabled|exported)(?:\(0x[0-9a-fA-F]+\))?=(.*)', line)
            if attribute:
                value = attribute[2]
                stack[-1][1].set(attribute[1], value.split('"')[1] if value.startswith('"') else value.strip())
    app = root.find('manifest/application')
    disabled = {'false', '0', '0x00000000'}
    if app is None or app.get('enabled') in disabled:
        return ''
    for category in ('android.intent.category.INFO', 'android.intent.category.LAUNCHER', 'com.oculus.intent.category.VR'):
        for component in app:
            if component.tag not in ('activity', 'activity-alias') or component.get('enabled') in disabled or component.get('exported') in disabled:
                continue
            name = component.get('name', '')
            if not name:
                continue
            for intent in component.findall('intent-filter'):
                actions = {action.get('name') for action in intent.findall('action')}
                categories = {item.get('name') for item in intent.findall('category')}
                if 'android.intent.action.MAIN' in actions and category in categories:
                    return package + name if name.startswith('.') else package + '.' + name if '.' not in name else name
    return ''

def _dump(tool, *args):
    return subprocess.run([str(tool), 'dump', *args], check=True,
        capture_output=True, encoding='utf-8', errors='replace', timeout=60,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout

def inspect(apk, sdk, allow_split=False):
    versions = [p for p in (sdk / 'build-tools').glob('*/aapt2.exe')]
    if not versions:
        raise RuntimeError('Android SDK build-tools (aapt2) are required')
    tool = max(versions, key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.parent.name)))
    output = _dump(tool, 'badging', str(apk))
    package = re.search(r"^package: name='([^']+)'", output, re.M)
    activity_match = re.search(r"^launchable-activity: name='([^']+)'", output, re.M)
    activity = activity_match[1] if activity_match else ''
    split = re.search(r"^package: .*\bsplit='([^']+)'", output, re.M)
    if package and not activity and not split:
        activity = _manifest_activity(_dump(tool, 'xmltree', str(apk), '--file', 'AndroidManifest.xml'), package[1])
    if not package or (not activity and not (allow_split and split)):
        raise RuntimeError('APK does not contain a launchable Android application')
    label = re.search(r"^application-label:'(.*)'$", output, re.M)
    version = re.search(r"versionName='([^']*)'", output)
    code = re.search(r"versionCode='([^']*)'", output)
    result = {'package': package[1], 'activity': package[1] + '/' + activity if activity else '',
        'split': split[1] if split else '', 'versionCode': code[1] if code else '',
        'name': label[1] if label else package[1], 'version': version[1] if version else '', 'image': ''}
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        result['patched'] = any('overport' in n.lower() or 'ovrport' in n.lower() for n in names)
        result['nativeAbis'] = sorted({n.split('/')[1] for n in names if n.startswith('lib/') and n.endswith('.so')})
        icons = re.findall(r"^application-icon-\d+:'([^']+)'", output, re.M)
        for icon in reversed(icons):
            if icon in names and icon.endswith('.png') and archive.getinfo(icon).file_size < 2 * 1024 * 1024:
                result['image'] = 'data:image/png;base64,' + base64.b64encode(archive.read(icon)).decode()
                break
    return result

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--apk', type=Path, required=True)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--allow-split', action='store_true')
    a = p.parse_args()
    try: print(json.dumps(inspect(a.apk, a.sdk, a.allow_split)))
    except Exception as error: raise SystemExit(str(error))
