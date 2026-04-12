import argparse
import jinja2
import ProtoParser


def main():
    arg_parser = argparse.ArgumentParser(description='PacketGenerator')
    arg_parser.add_argument('--path', type=str, required=True, help='proto file path')
    arg_parser.add_argument('--output', type=str, required=True, help='output file name (without extension)')
    arg_parser.add_argument('--recv', type=str, default='C_', help='recv prefix')
    arg_parser.add_argument('--send', type=str, default='S_', help='send prefix')
    args = arg_parser.parse_args()

    parser = ProtoParser.ProtoParser(1000, args.recv, args.send)
    parser.parse_proto(args.path)

    file_loader = jinja2.FileSystemLoader('Templates')
    env = jinja2.Environment(loader=file_loader)

    template = env.get_template('PacketHandler.h')
    output = template.render(parser=parser, output=args.output)

    with open(args.output + '.h', 'w') as f:
        f.write(output)

    print(f'Generated {args.output}.h')


if __name__ == '__main__':
    main()