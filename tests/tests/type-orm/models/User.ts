import { Entity, Column, PrimaryGeneratedColumn } from 'typeorm/browser';

@Entity('user')
export class User {
  @PrimaryGeneratedColumn('uuid')
  id!: string;

  @Column('text')
  name!: string;

  @Column('int')
  age!: number;

  @Column('float')
  networth!: number;

  @Column('simple-json')
  metadata: { nickname: string };

  @Column('blob')
  avatar: ArrayBuffer;
}
