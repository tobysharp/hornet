from pathlib import Path

SVG_W, LM, RM = 960, 36, 36
CONTENT_W = SVG_W - LM - RM
ROW_H, GUTTER, FONT_SIZE, RADIUS = 42, 12, 14, 10  # RADIUS adds rounded corners

fill_hornet = "#eef6ff"
fill_node = "#ffeecc"
stroke = "#e5e7eb"
text_color = "#111827"

def rect(x, y, w, h, fill):
    return f'<rect x="{x:.1f}" y="{y:.1f}" rx="{RADIUS}" ry="{RADIUS}" width="{w:.1f}" height="{h:.1f}" fill="{fill}" stroke="{stroke}"/>'

def text(x, y, label, h=ROW_H):
    return f'<text x="{x:.1f}" y="{y + h/2:.1f}" text-anchor="middle" font-size="{FONT_SIZE}" fill="{text_color}">{label}</text>'

def rounded_path(points, r, fill):
    """
    Rounded polygon path with arcs in the correct sweep direction.
    Points must be listed CCW.
    """
    def seglen(a,b): return ((a[0]-b[0])**2 + (a[1]-b[1])**2)**0.5
    n=len(points)
    cmds=[]
    for i,(x,y) in enumerate(points):
        p=points[i-1]
        q=points[(i+1)%n]
        # unit vectors
        vx,vy=x-p[0],y-p[1]; L=(vx*vx+vy*vy)**0.5 or 1; ux,uy=vx/L,vy/L
        vx2,vy2=q[0]-x,q[1]-y; L2=(vx2*vx2+vy2*vy2)**0.5 or 1; ux2,uy2=vx2/L2,vy2/L2
        # entry/exit points
        bx,by=x-ux*r,y-uy*r
        ax,ay=x+ux2*r,y+uy2*r
        if i==0: cmds.append(f"M {bx:.1f} {by:.1f}")
        else:    cmds.append(f"L {bx:.1f} {by:.1f}")
        # cross product decides sweep
        cross=ux*uy2-uy*ux2
        sweep=1 if cross>0 else 0
        cmds.append(f"A {r} {r} 0 0 {sweep} {ax:.1f} {ay:.1f}")
    cmds.append("Z")
    return f'<path d="{" ".join(cmds)}" fill="{fill}" stroke="{stroke}"/>'

rows = [
    ["hornet::util"],
    ["hornet::crypto", "hornet::encoding", "hornet::node::util"],
    ["hornet::protocol", "hornet::node::net"],
    ["hornet::consensus"],
    ["hornet::data"],
    ["hornet::node::dispatch"],
    ["hornet::node::sync"]
]
total_rows = len(rows)
row_y = {idx:(total_rows-1-idx)*(ROW_H+5)+4 for idx in range(total_rows)}

parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SVG_W}" height="{row_y[0]+ROW_H+120}" viewBox="0 0 {SVG_W} {row_y[0]+ROW_H+120}">']
parts.append(f'<rect x="0" y="0" width="{SVG_W}" height="{row_y[0]+ROW_H+120}" opacity="0" fill="#ffffff"/>')
parts.append('<style>text{font-family:Inter, sans-serif;dominant-baseline:middle;}</style>')

# Row 0: util
parts += [rect(LM, row_y[0], CONTENT_W, ROW_H, fill_hornet),
          text(LM+CONTENT_W/2, row_y[0], "hornet::util")]

# Row 1: crypto, encoding, node::util
crypto_w = encoding_w = CONTENT_W*0.25
nodeutil_w = CONTENT_W - crypto_w - encoding_w - 2*GUTTER
crypto_x = LM
encoding_x = crypto_x + crypto_w + GUTTER
nodeutil_x = encoding_x + encoding_w + GUTTER
parts += [rect(crypto_x, row_y[1], crypto_w, ROW_H, fill_hornet),
          text(crypto_x+crypto_w/2, row_y[1], "hornet::crypto"),
          rect(encoding_x, row_y[1], encoding_w, ROW_H, fill_hornet),
          text(encoding_x+encoding_w/2, row_y[1], "hornet::encoding"),
          rect(nodeutil_x, row_y[1], nodeutil_w, ROW_H, fill_node),
          text(nodeutil_x+nodeutil_w/2, row_y[1], "hornet::node::util")]

# Row 2: protocol & net
proto_left = crypto_x + crypto_w/3
proto_right = encoding_x + encoding_w
proto_w = proto_right - proto_left
net_x, net_w = nodeutil_x, nodeutil_w
net_bottom = row_y[2] + ROW_H
cons_top = row_y[3]
parts += [rect(proto_left, row_y[2], proto_w, ROW_H, fill_hornet),
          text(proto_left+proto_w/2, row_y[2], "hornet::protocol"),
          rect(net_x, cons_top, net_w, net_bottom-cons_top, fill_node),
          text(net_x+net_w/2, cons_top, "hornet::node::net", h=net_bottom-cons_top)]

# Row 3: consensus (use exactly your p1..p6, CCW)
cons_top_y, cons_bottom_y = row_y[3], row_y[3]+ROW_H
p1 = (proto_right - crypto_w/3, cons_top_y)
p2 = (crypto_x, cons_top_y)
p3 = (crypto_x, row_y[2]+ROW_H)
p4 = (proto_left-GUTTER, row_y[2]+ROW_H)
p5 = (proto_left-GUTTER, cons_bottom_y)
p6 = (proto_right - crypto_w/3, cons_bottom_y)
parts.append(rounded_path([p1,p2,p3,p4,p5,p6], RADIUS, fill_hornet))
parts.append(text((proto_left+proto_right)/2, row_y[3], "hornet::consensus"))

# Row 4: data (use your q1..q6 exactly)
q1 = (proto_right, row_y[4])
q2 = (crypto_x, q1[1])
q3 = (crypto_x, row_y[4]+ROW_H)
q4 = (p1[0] + GUTTER, row_y[4]+ROW_H)  # as in your script
q5 = (q4[0], cons_bottom_y)
q6 = (proto_right, cons_bottom_y)
parts.append(rounded_path([q1,q2,q3,q4,q5,q6], RADIUS, fill_hornet))
parts.append(text((proto_left+proto_right)/2, row_y[4], "hornet::data"))

# Row 5: dispatch (your L points, CCW)
y5, y5b, y4b = row_y[5], row_y[5]+ROW_H, row_y[4]+ROW_H
util_left = nodeutil_x; rightX = LM+CONTENT_W
dpts = [
    (crypto_x + crypto_w/2, y5),
    (rightX, y5),
    (rightX, y4b),
    (util_left, y4b),
    (util_left, y5b),
    (crypto_x + crypto_w/2, y5b),
]
parts.append(rounded_path(dpts, RADIUS, fill_node))
parts.append(text(LM+CONTENT_W/2, y5, "hornet::node::dispatch"))

# Row 6: sync (your s1..s6, CCW)
s1 = (LM+CONTENT_W, row_y[6])
s2 = (LM, s1[1])
s3 = (LM, row_y[5]+ROW_H)
s4 = (crypto_x + crypto_w/2 - GUTTER, s3[1])
s5 = (s4[0], row_y[6] + ROW_H)
s6 = (s1[0], s5[1])
parts.append(rounded_path([s1,s2,s3,s4,s5,s6], RADIUS, fill_node))
parts.append(text(LM + CONTENT_W/2, row_y[6], "hornet::sync"))

# Legend
legend_y = row_y[0] + ROW_H + 20
parts += [f'<rect x="36" y="{legend_y}" rx="{RADIUS}" ry="{RADIUS}" width="14" height="14" fill="{fill_hornet}" stroke="{stroke}"/>',
          f'<text x="58" y="{legend_y+7}" fill="{text_color}" font-size="12" text-anchor="start">hornetlib</text>',
          f'<rect x="162" y="{legend_y}" rx="{RADIUS}" ry="{RADIUS}" width="14" height="14" fill="{fill_node}" stroke="{stroke}"/>',
          f'<text x="184" y="{legend_y+7}" fill="{text_color}" font-size="12" text-anchor="start">hornetnodelib</text>']

parts.append('</svg>')

Path("layers.svg").write_text("\n".join(parts), encoding="utf-8")
