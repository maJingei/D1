import argparse
import os
import jinja2
import ProtoParser

def main():
    _script_dir = os.path.dirname(os.path.abspath(__file__))
    _default_proto = os.path.normpath(os.path.join(_script_dir, '..', '..', 'Common', 'protoBuf', 'Protocol.proto'))

    arg_parser = argparse.ArgumentParser(description='PacketGenerator')
    arg_parser.add_argument('--path', type=str, default=_default_proto, help='proto path')
    arg_parser.add_argument('--output', type=str, default='PacketHandler', help='output file name (no extension)')
    arg_parser.add_argument('--recv', type=str, default='C_', help='recv prefix convention')
    arg_parser.add_argument('--send', type=str, default='S_', help='send prefix convention')
    args = arg_parser.parse_args()

    parser = ProtoParser.ProtoParser(1000, args.recv, args.send)
    parser.parse_proto(args.path)

    template_dir = os.path.join(_script_dir, 'Templates')
    file_loader = jinja2.FileSystemLoader(template_dir)
    env = jinja2.Environment(loader=file_loader)

    template = env.get_template('PacketHandler.h')
    output = template.render(parser=parser, output=os.path.basename(args.output))

    with open(args.output + '.h', 'w', encoding='utf-8') as f:
        f.write(output)

    print(output)

if __name__ == '__main__':
    main()